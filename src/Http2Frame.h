#ifndef WEBSOCKETCLIENT_HTTP2FRAME_H_
#define WEBSOCKETCLIENT_HTTP2FRAME_H_

#include <Arduino.h>

class Http2Frame {
 public:
  static const char* HTTP2_CONNECTION_PREFACE;
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
    uint8_t* buffer = nullptr;
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
    static SettingFrameField fromBytes(const uint8_t* data, std::size_t* size);
  };

  struct SettingsFramePayload: HeaderFrame {
    SettingsFramePayload(std::initializer_list<SettingFrameField> init);
    ~SettingsFramePayload();
    static SettingsFramePayload fromBytes(const uint8_t* data, const std::size_t size);
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
    static uint32_t decodeUnsignedInteger(uint8_t prefixBits, const uint8_t** p);
    static String decodeEncodedString(const uint8_t** p);
    static HeaderFrameField fromBytes(const uint8_t* data, std::size_t* size);
  };

  struct HeadersFramePayload: HeaderFrame {
    HeadersFramePayload(std::initializer_list<HeaderFrameField> init);
    ~HeadersFramePayload();
    static HeadersFramePayload fromBytes(const uint8_t* data, const std::size_t size);
  };

  std::size_t buffer_size;
  uint8_t* buffer = nullptr;

  Http2Frame(FrameType type, FrameFlags flags, StreamIdentifier streamId, std::size_t payloadLength, const uint8_t* payload = NULL);
  virtual ~Http2Frame();

  const std::uint8_t* toBytes() const;
  std::size_t bytesSize() const;
  bool isValidHeader() const;
  bool isValidAll() const;
  FrameType getType() const;
  FrameFlags getFlags() const;
  StreamIdentifier getStreamId() const;
  std::uint32_t getPayloadLength() const;
  std::uint32_t getFrameLength() const;
  const uint8_t *getPayload() const;

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
      void init();
      bool subtract(size_t len);
  } receivingData;

  void init();
  Http2Status() {
    init();
  }
};

#endif
