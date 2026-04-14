#include "Http2Frame.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif

#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
#define HTTP2_ALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define HTTP2_FREE(p) heap_caps_free((p))
#else
#define HTTP2_ALLOC(sz) new uint8_t[(sz)]
#define HTTP2_FREE(p) delete[] (p)
#endif

const char* Http2Frame::HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

static void hexdump(const void *mem, uint32_t len, uint8_t cols = 16) {
#ifdef DEBUGGING
	const uint8_t* src = (const uint8_t*) mem;
	Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		Serial.printf("%02X ", *src);
		src++;
	}
	Serial.printf("\n");
#endif
}

Http2Frame::SettingFrameField Http2Frame::SettingFrameField::fromBytes(const uint8_t* data, std::size_t* size) {
  if (*size < (2+4)) {
    *size = 0;
    return SettingFrameField(SETTINGS_HEADER_TABLE_SIZE, 0);  // invalid
  }
  *size = (2+4);
  const SettingIdentifier id = static_cast<SettingIdentifier>((data[0] << 8) | data[1]);
  const std::uint32_t value = ((std::uint32_t)data[2] << 24) | ((std::uint32_t)data[3] << 16) | ((std::uint32_t)data[4] << 8) | (std::uint32_t)data[5];
  return SettingFrameField(id, value);
}

Http2Frame::SettingsFramePayload::SettingsFramePayload(std::initializer_list<Http2Frame::SettingFrameField> init) {
  buffer_size = 0;
  for (const auto& fragment : init) {
    (void)fragment;
    buffer_size += 2; // for setting id
    buffer_size += 4; // for setting value
  }
  buffer = static_cast<uint8_t*>(HTTP2_ALLOC(buffer_size));
  if (buffer == nullptr && buffer_size > 0) {
    log_e("SettingsFramePayload alloc failed size=%u", static_cast<unsigned>(buffer_size));
    return;
  }
  uint8_t* p = buffer;
  for (const auto& fragment : init) {
    *p++ = (uint8_t)(fragment.id >> 8);
    *p++ = (uint8_t)(fragment.id & 0xFF);
    *p++ = (uint8_t)(fragment.value >> 24);
    *p++ = (uint8_t)((fragment.value >> 16) & 0xFF);
    *p++ = (uint8_t)((fragment.value >> 8) & 0xFF);
    *p++ = (uint8_t)(fragment.value & 0xFF);
  }
}

Http2Frame::SettingsFramePayload::~SettingsFramePayload() {
  HTTP2_FREE(buffer);
}
Http2Frame::SettingsFramePayload Http2Frame::SettingsFramePayload::fromBytes(const uint8_t* data, const std::size_t size) {
  SettingsFramePayload payload{};
  payload.buffer_size = size;
  payload.buffer = static_cast<uint8_t*>(HTTP2_ALLOC(size));
  if (payload.buffer == nullptr && size > 0) {
    log_e("SettingsFramePayload::fromBytes alloc failed size=%u", static_cast<unsigned>(size));
    payload.buffer_size = 0;
    return payload;
  }
  memcpy(payload.buffer, data, size);
  return payload;
}

uint32_t Http2Frame::HeaderFrameField::decodeUnsignedInteger(uint8_t prefixBits, const uint8_t** p) {
  const uint8_t* ptr = *p;
  const uint8_t prefixMax = (1 << prefixBits) - 1;
  uint32_t value = (*ptr) & prefixMax;
  ptr++;
  log_v("Decoded prefix value: %d %x %x", value, value, prefixMax);
  if (value == prefixMax) {
    uint32_t m = 0;
    uint8_t b;
    do {
      b = *ptr;
      ptr++;
      value += ((b & 0x7F) << m);
      m += 7;
    } while (b & 0x80);
  }
  *p = ptr;
  return value;
}

String Http2Frame::HeaderFrameField::decodeEncodedString(const uint8_t** p) {
  const bool huffmanEncoded = **p & 0b10000000;  // Huffman encoding flag
  const std::size_t length = decodeUnsignedInteger(7, p);
  const uint8_t* ptr = *p;
  if (huffmanEncoded) {
    // log_w("Huffman decoding not implemented, skipping %u bytes", length);
    for (std::size_t i = 0; i < length; i++) {
      const uint8_t current_byte = *ptr;
      (void)current_byte;
      ptr++;
    }
    *p = ptr;
    return "****";
  } else {
    String v;
    for (std::size_t i = 0; i < length; i++) {
      v += *ptr;
      ptr++;
    }
    *p = ptr;
    return v;
  }
}

Http2Frame::HeaderFrameField Http2Frame::HeaderFrameField::fromBytes(const uint8_t* data, std::size_t* size) {
  const uint8_t* p = data;
  if ((*p & tableSizeFieldMask) == tableSizeField) {
    const uint32_t tableSize = decodeUnsignedInteger(5, &p);  // skip tableSizeField prefix
    (void)tableSize;
  }
  uint32_t index = 0;
  if ((*p & indexedHeaderFieldMask) == indexedHeaderField) {  // indexedHeaderField
    index = decodeUnsignedInteger(7, &p);  // skip indexedHeaderField prefix
  } else if ((*p & literalIndexedHeaderField) == literalIndexedHeaderField) {
    index = decodeUnsignedInteger(6, &p);  // skip literalIndexedHeaderField prefix
  } else if ((*p & literalUnindexedHeaderFieldMask) == literalUnindexedHeaderField) {
    index = decodeUnsignedInteger(4, &p);  // skip literalUnindexedHeaderField prefix
  } else if ((*p & literalHeaderFieldWithoutIndexingMask) == literalHeaderFieldWithoutIndexing) {
    index = decodeUnsignedInteger(4, &p);  // skip literalHeaderFieldWithoutIndexing prefix
  } else {
    *size = 0;  // error
    return HeaderFrameField("", "");
  }
  const uint32_t staticTableSize = 62;
  const char* staticTable[staticTableSize][2] = {
    {"", ""},  // dummy for index 0
    {":authority", "" },
    {":method", "GET" },
    {":method", "POST" },
    {":path", "/" },
    {":path", "/index.html" },
    {":scheme", "http" },
    {":scheme", "https" },
    {":status", "200" },
    {":status", "204" },
    {":status", "206" },
    {":status", "304" },
    {":status", "400" },
    {":status", "404" },
    {":status", "500" },
    {"accept-charset", "" },
    {"accept-encoding", "gzip, deflate" },
    {"accept-language", "" },
    {"accept-ranges", "" },
    {"accept", "" },
    {"access-control-allow-origin", "" },
    {"age", "" },
    {"allow", "" },
    {"authorization", "" },
    {"cache-control", "" },
    {"content-disposition", "" },
    {"content-encoding", "" },
    {"content-language", "" },
    {"content-length", "" },
    {"content-location", "" },
    {"content-range", "" },
    {"content-type", "" },
    {"cookie", "" },
    {"date", "" },
    {"etag", "" },
    {"expect", "" },
    {"expires", "" },
    {"from", "" },
    {"host", "" },
    {"if-match", "" },
    {"if-modified-since", "" },
    {"if-none-match", "" },
    {"if-range", "" },
    {"if-unmodified-since", "" },
    {"last-modified", "" },
    {"link", "" },
    {"location", "" },
    {"max-forwards", "" },
    {"proxy-authenticate", "" },
    {"proxy-authorization", "" },
    {"range", "" },
    {"referer", "" },
    {"refresh", "" },
    {"retry-after", "" },
    {"server", "" },
    {"set-cookie", "" },
    {"strict-transport-security", "" },
    {"transfer-encoding", "" },
    {"user-agent", "" },
    {"vary", "" },
    {"via", "" },
    {"www-authenticate", "" },
  };
  String name = "";
  String value = "";
  if (index < staticTableSize) {
    log_v("Header index: %u", index);
    name = staticTable[index][0];
    value = staticTable[index][1];
  }
  if (name.length() == 0) {
    name = decodeEncodedString(&p);
  }
  if (value.length() == 0) {
    value = decodeEncodedString(&p);
  }
  *size = (p-data);
  return HeaderFrameField(name.c_str(), value.c_str());
}

Http2Frame::HeadersFramePayload::HeadersFramePayload(std::initializer_list<Http2Frame::HeaderFrameField> init) {
  buffer_size = 0;
  for (const auto& fragment : init) {
    buffer_size += 1; // for literalHeaderFieldWithoutIndexing byte
    buffer_size += 1; // for name length byte
    assert(fragment.name.length() <= 0b01111110);
    buffer_size += fragment.name.length();
    buffer_size += 1; // for value length byte
    assert(fragment.value.length() <= 0b01111110);
    buffer_size += fragment.value.length();
  }
  buffer = static_cast<uint8_t*>(HTTP2_ALLOC(buffer_size));
  if (buffer == nullptr && buffer_size > 0) {
    log_e("HeadersFramePayload alloc failed size=%u", static_cast<unsigned>(buffer_size));
    return;
  }
  uint8_t* p = buffer;
  for (const auto& fragment : init) {
    log_d("Header: %s: %s", fragment.name.c_str(), fragment.value.c_str());
    *p++ = HeaderFrameField::literalHeaderFieldWithoutIndexing; // literalHeaderFieldWithoutIndexing
    *p++ = static_cast<uint8_t>(fragment.name.length());
    memcpy(p, fragment.name.c_str(), fragment.name.length());
    p += fragment.name.length();
    *p++ = static_cast<uint8_t>(fragment.value.length());
    memcpy(p, fragment.value.c_str(), fragment.value.length());
    p += fragment.value.length();
  }
}
Http2Frame::HeadersFramePayload::~HeadersFramePayload() {
  HTTP2_FREE(buffer);
}
Http2Frame::HeadersFramePayload Http2Frame::HeadersFramePayload::fromBytes(const uint8_t* data, const std::size_t size) {
  HeadersFramePayload payload{};
  payload.buffer_size = size;
  payload.buffer = static_cast<uint8_t*>(HTTP2_ALLOC(size));
  if (payload.buffer == nullptr && size > 0) {
    log_e("HeadersFramePayload::fromBytes alloc failed size=%u", static_cast<unsigned>(size));
    payload.buffer_size = 0;
    return payload;
  }
  memcpy(payload.buffer, data, size);
  return payload;
}

Http2Frame::Http2Frame(Http2Frame::FrameType type, FrameFlags flags, StreamIdentifier streamId, std::size_t payloadLength, const uint8_t* payload):
 buffer_size(Http2Frame::Http2FrameHeaderSize + (payload?payloadLength:0)),
 buffer(static_cast<uint8_t*>(HTTP2_ALLOC(buffer_size))) {
  if (buffer == nullptr && buffer_size > 0) {
    log_e("Http2Frame alloc failed size=%u", static_cast<unsigned>(buffer_size));
    buffer_size = 0;
    return;
  }
  assert((streamId==0U || streamId%2==1) && streamId!=0x80000000U);  // must be 0 or odd for client-initiated frames 
  buffer[0] = (payloadLength >> 16) & 0xFF;
  buffer[1] = (payloadLength >> 8) & 0xFF;
  buffer[2] = payloadLength & 0xFF;
  buffer[3] = static_cast<std::uint8_t>(type);
  buffer[4] = static_cast<std::uint8_t>(flags);
  buffer[5] = ((std::uint32_t)streamId >> 24) & 0xFF;
  buffer[6] = ((std::uint32_t)streamId >> 16) & 0xFF;
  buffer[7] = ((std::uint32_t)streamId >> 8) & 0xFF;
  buffer[8] = ((std::uint32_t)streamId) & 0xFF;
  if(payload) memcpy(buffer + Http2Frame::Http2FrameHeaderSize, payload, payloadLength);  // deep copy
};

Http2Frame::~Http2Frame() {
  HTTP2_FREE(buffer);
}

const std::uint8_t* Http2Frame::toBytes() const {
  return buffer;
}
std::size_t Http2Frame::bytesSize() const {
  return buffer_size;
}

bool Http2Frame::isValidHeader() const {
  return Http2FrameHeaderSize <= buffer_size;
}

bool Http2Frame::isValidAll() const {
  return isValidHeader() && (Http2FrameHeaderSize + getPayloadLength() == buffer_size);
}

Http2Frame::FrameType Http2Frame::getType() const {
  return static_cast<FrameType>(buffer[3]);
}
Http2Frame::FrameFlags Http2Frame::getFlags() const {
  return static_cast<FrameFlags>(buffer[4]);
}
Http2Frame::StreamIdentifier Http2Frame::getStreamId() const {
  return ((std::uint32_t)(buffer[5] & 0x7F) << 24) | ((std::uint32_t)buffer[6] << 16) | ((std::uint32_t)buffer[7] << 8) | (std::uint32_t)buffer[8];
}
std::uint32_t Http2Frame::getPayloadLength() const {
  return ((std::uint32_t)buffer[0] << 16) | ((std::uint32_t)buffer[1] << 8) | (std::uint32_t)buffer[2];
}
std::uint32_t Http2Frame::getFrameLength() const {
  return Http2FrameHeaderSize + getPayloadLength();
}
const uint8_t *Http2Frame::getPayload() const {
  return isValidAll() ? (buffer + Http2FrameHeaderSize): nullptr;
}

Http2Frame Http2Frame::fromBytes(const uint8_t* data, std::size_t size) {
  if (size < Http2Frame::Http2FrameHeaderSize) {
    return Http2Frame();
  }
  const std::size_t payloadLength = ((std::uint32_t)data[0] << 16) | ((std::uint32_t)data[1] << 8) | (std::uint32_t)data[2];
  const FrameType type = static_cast<FrameType>(data[3]);
  const FrameFlags flags = static_cast<FrameFlags>(data[4]);
  const StreamIdentifier streamId = static_cast<StreamIdentifier>(((std::uint32_t)(data[5] & 0x7F) << 24) | ((std::uint32_t)data[6] << 16) | ((std::uint32_t)data[7] << 8) | (std::uint32_t)data[8]);
  log_v("Http2Frame::fromBytes: type=%d, flags=%02x, streamId=%u, payloadLength=%u, size=%u", type, flags, streamId, payloadLength, size);
  if((streamId==0U || streamId%2==1) && streamId!=0x80000000U) {
    if (size < Http2Frame::Http2FrameHeaderSize + payloadLength) {
      return Http2Frame(type, flags, streamId, payloadLength, nullptr);
    }
    const uint8_t* payload = (0 < payloadLength)?(data + Http2Frame::Http2FrameHeaderSize):nullptr;
    return Http2Frame(type, flags, streamId, payloadLength, payload);
  } else {
    hexdump(data, size);
    return Http2Frame();
  }
}

void Http2Status::ReceivingData::init() {
  streamId = 0;
  remainLength = 0;
  endStream = false;
}

bool Http2Status::ReceivingData::subtract(size_t len) {
  if (len <= remainLength) {
    remainLength -= len;
  } else {
    remainLength = 0;
  }
  if (remainLength == 0) {
    streamId = 0;
    return endStream;
  }
  return false;
}

void Http2Status::init() {
settingsReceived = false;
settingsSent = false;
enableConnectProtocol = false;
serverMaxConcurrentStreams = UINT32_MAX;

serverMaxFrameSize = 16384;
serverInitialWindowSize = UINT16_MAX;
serverWindowSize = 0;

clientMaxFrameSize = 16384;
clientInitialWindowSize = 16384;
clientWindowSize = 0;

totalTxSize = 0;
totalRxSize = 0;  // total received size

clientInitialWindowKeepSize = clientInitialWindowSize-(clientInitialWindowSize/4);

connected = false;
receivingData.init();
}
