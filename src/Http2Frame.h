#ifndef WEBSOCKETCLIENT_HTTP2FRAME_H_
#define WEBSOCKETCLIENT_HTTP2FRAME_H_

#include <Arduino.h>

class Http2Frame {
 public:
  const static size_t Http2FrameHeaderSize = 9;
  enum FrameType {
    FRAME_TYPE_DATA = 0x0,
    FRAME_TYPE_HEADERS = 0x1,
    FRAME_TYPE_PRIORITY = 0x2,
    FRAME_TYPE_RST_STREAM = 0x3,
    FRAME_TYPE_SETTINGS = 0x4,
    FRAME_TYPE_PUSH_PROMISE = 0x5,
    FRAME_TYPE_PING = 0x6,
    FRAME_TYPE_GOAWAY = 0x7,
    FRAME_TYPE_WINDOW_UPDATE = 0x8,
    FRAME_TYPE_CONTINUATION = 0x9,
  };
  enum FrameFlags {
    FRAME_FLAG_NONE = 0x0,
    FRAME_FLAG_ACK = 0x1,
    FRAME_FLAG_END_STREAM = 0x1,  // end of HEADERS for DATA frame
    FRAME_FLAG_END_HEADERS = 0x4,
    FRAME_FLAG_PADDED = 0x8,
    FRAME_FLAG_PRIORITY = 0x20
  };
  enum SettingIdentifier {
    SETTINGS_HEADER_TABLE_SIZE = 0x1,
    SETTINGS_ENABLE_PUSH = 0x2,
    SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    SETTINGS_INITIAL_WINDOW_SIZE = 0x4,
    SETTINGS_MAX_FRAME_SIZE = 0x5,
    SETTINGS_MAX_HEADER_LIST_SIZE = 0x6,
    SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x8  // RFC 8441
  };
  typedef uint32_t StreamIdentifier;

  struct HeaderFrame {
    std::size_t buffer_size;
    uint8_t* buffer;
    const std::uint8_t* toBytes() const {
      return buffer;
    }
    std::size_t bytesSize() const {
      return buffer_size;
    }
  };

  struct SettingFrameField {
    SettingFrameField(SettingIdentifier id, uint32_t value) : id(id), value(value) {
    }
    const std::uint16_t id;
    const std::uint32_t value;
    static SettingFrameField fromBytes(const uint8_t* data, std::size_t* size) {
      *size += (2+4);
      const SettingIdentifier id = static_cast<SettingIdentifier>((data[0] << 8) | data[1]);
      const std::uint32_t value = ((std::uint32_t)data[2] << 24) | ((std::uint32_t)data[3] << 16) | ((std::uint32_t)data[4] << 8) | (std::uint32_t)data[5];
      return SettingFrameField(id, value);
    }
  };

  struct SettingsFramePayload: HeaderFrame {
    SettingsFramePayload(std::initializer_list<SettingFrameField> init) {
      buffer_size = 0;
      for (const auto& fragment : init) {
        buffer_size += 2; // for setting id
        buffer_size += 4; // for setting value
      }
      buffer = new uint8_t[buffer_size];
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
    ~SettingsFramePayload() {
      delete[] buffer;
    }
    static SettingsFramePayload fromBytes(const uint8_t* data, const std::size_t size) {
      SettingsFramePayload payload{};
      payload.buffer_size = size;
      payload.buffer = new uint8_t[size];
      memcpy(payload.buffer, data, size);
      return payload;
    }
  };

  

  struct HeaderFrameField {
    const String name;
    const String value;
    static const uint8_t indexedHeaderField = 0b10000000;  // 0x80
    static const uint8_t indexedHeaderFieldMask = 0b10000000;
    static const uint8_t literalIndexedHeaderField = 0b01000000;  // 0x40
    static const uint8_t literalIndexedHeaderFieldMask = 0b11000000;
    static const uint8_t tableSizeField = 0b00100000;  // 0x20
    static const uint8_t tableSizeFieldMask = 0b11100000;
    static const uint8_t literalUnindexedHeaderField = 0b00010000;  // 0x10
    static const uint8_t literalUnindexedHeaderFieldMask = 0b11110000;
    static const uint8_t literalHeaderFieldWithoutIndexing = 0b00000000;
    static const uint8_t literalHeaderFieldWithoutIndexingMask = 0b11110000;
    HeaderFrameField(const char *name, const char *value): name(name), value(value) {
    }
    ~HeaderFrameField() {
    }
      
    static uint32_t decodeUnsignedInteger(uint8_t prefixBits, const uint8_t** p) {
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

    static String decodeEncodedString(const uint8_t** p) {
      const bool huffmanEncoded = **p & 0b10000000;  // Huffman encoding flag
      const std::size_t length = decodeUnsignedInteger(7, p);
      const uint8_t* ptr = *p;
      if (huffmanEncoded) {
        // log_w("Huffman decoding not implemented, skipping %u bytes", length);
        for (std::size_t i = 0; i < length; i++) {
          uint8_t current_byte = *ptr;
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

    static HeaderFrameField fromBytes(const uint8_t* data, std::size_t* size) {
      const uint8_t* p = data;
      if ((*p & tableSizeFieldMask) == tableSizeField) {
        const uint32_t tableSize = decodeUnsignedInteger(5, &p);  // skip tableSizeField prefix
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
      *size += (p-data);
      return HeaderFrameField(name.c_str(), value.c_str());
    }
  };

  struct HeadersFramePayload: HeaderFrame {
    HeadersFramePayload(std::initializer_list<HeaderFrameField> init) {
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
      buffer = new uint8_t[buffer_size];
      uint8_t* p = buffer;
      for (const auto& fragment : init) {
        *p++ = HeaderFrameField::literalHeaderFieldWithoutIndexing; // literalHeaderFieldWithoutIndexing
        *p++ = static_cast<uint8_t>(fragment.name.length());
        memcpy(p, fragment.name.c_str(), fragment.name.length());
        p += fragment.name.length();
        *p++ = static_cast<uint8_t>(fragment.value.length());
        memcpy(p, fragment.value.c_str(), fragment.value.length());
        p += fragment.value.length();
      }
    }
    ~HeadersFramePayload() {
      delete[] buffer;
    }
    static HeadersFramePayload fromBytes(const uint8_t* data, const std::size_t size) {
      HeadersFramePayload payload{};
      payload.buffer_size = size;
      payload.buffer = new uint8_t[size];
      memcpy(payload.buffer, data, size);
      return payload;
    }
  };

  std::size_t buffer_size;
  uint8_t* buffer;

  Http2Frame(FrameType type, FrameFlags flags, StreamIdentifier streamId, std::size_t payloadLength, const uint8_t* payload = NULL);
  virtual ~Http2Frame();

  const std::uint8_t* toBytes() const {
    return buffer;
  }
  std::size_t bytesSize() const {
    return buffer_size;
  }

  bool isValidHeader() const {
    return Http2FrameHeaderSize <= buffer_size;
  }

  bool isValidAll() const {
    return isValidHeader() && (Http2FrameHeaderSize + getPayloadLength() == buffer_size);
  }

  FrameType getType() const {
    return static_cast<FrameType>(buffer[3]);
  }
  FrameFlags getFlags() const {
    return static_cast<FrameFlags>(buffer[4]);
  }
  StreamIdentifier getStreamId() const {
    return ((std::uint32_t)(buffer[5] & 0x7F) << 24) | ((std::uint32_t)buffer[6] << 16) | ((std::uint32_t)buffer[7] << 8) | (std::uint32_t)buffer[8];
  }
  std::uint32_t getPayloadLength() const {
    return ((std::uint32_t)buffer[0] << 16) | ((std::uint32_t)buffer[1] << 8) | (std::uint32_t)buffer[2];
  }
  std::uint32_t getFrameLength() const {
    return Http2FrameHeaderSize + getPayloadLength();
  }
  const uint8_t *getPayload() const {
    return isValidAll() ? (buffer + Http2FrameHeaderSize): nullptr;
  }

  static Http2Frame fromBytes(const uint8_t* data, std::size_t size);
 protected:
  Http2Frame() : buffer_size(0), buffer(nullptr) {
    // Empty
  }
};

struct Http2Status {
  bool settingsReceived;
  bool settingsSent;
  bool enableConnectProtocol;
  uint32_t serverMaxConcurrentStreams;
  uint32_t serverInitialWindowSize;
  uint32_t serverMaxFrameSize;
  uint32_t serverWindowSize;

  uint32_t clientInitialWindowSize;
  uint32_t clientMaxFrameSize;
  uint32_t clientWindowSize;

  uint32_t clientInitialWindowKeepSize;

  uint32_t totalRxSize;
  uint32_t totalTxSize;
  bool connected;
  struct ReceivingData {
      Http2Frame::StreamIdentifier streamId;
      size_t remainLength;
      bool endStream;
      ReceivingData(Http2Frame::StreamIdentifier streamId = 0, size_t remainLength = 0) : streamId(streamId), remainLength(remainLength) {
      }
      void init() {
        streamId = 0;
        remainLength = 0;
        endStream = false;
      }
      bool subtract(size_t len) {
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
  } receivingData;

  void init() {
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
  Http2Status() {
    init();
  }
};

#endif
