#ifndef WEBSOCKETCLIENT_HTTP1HEADER_H_
#define WEBSOCKETCLIENT_HTTP1HEADER_H_

#include <Arduino.h>

class Http1Header {
 public:
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
  struct RequestMethod: HeaderFrame {
    RequestMethod(const char *method, const char* path);
    ~RequestMethod();
  };
  struct ResonseStatus: HeaderFrame {
    ResonseStatus(int statusCode, const char* statusMessage);
    ~ResonseStatus();
    bool isValid() const;
    static ResonseStatus fromBytes(const uint8_t* data, std::size_t* size);
    ResonseStatus() {
      buffer_size=0;
      buffer = nullptr;
      // Empty
    };
    int statusCode = 0;
    String statusMessage = "";
  };
  struct HeaderField {
    const String name;
    const String value;
    enum State {
      Valid,
      Invalid,
      EndOfHeaders
    } state;
    HeaderField(const char *name, const char *value): name(name), value(value), state(Valid) {
    }
    HeaderField(State state): state(state) {
    }
    static HeaderField fromBytes(const uint8_t* data, std::size_t* size);
  };
  struct HeaderFieldPayload: HeaderFrame {
    HeaderFieldPayload(std::initializer_list<HeaderField> init);
    ~HeaderFieldPayload();
  };
  std::size_t buffer_size;
  uint8_t* const buffer;

  Http1Header(const RequestMethod& method, const HeaderFieldPayload& headerPayload);
  virtual ~Http1Header();

  const std::uint8_t* toBytes() const {
    return buffer;
  }
  std::size_t bytesSize() const {
    return buffer_size;
  }
};
#endif
