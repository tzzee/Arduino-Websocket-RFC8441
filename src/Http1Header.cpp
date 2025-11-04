#include "Http1Header.h"

#define CRLF "\r\n"
#define HTTP1_VERSION_1_0  "HTTP/1.0"
#define HTTP1_VERSION_1_1  "HTTP/1.1"

Http1Header::RequestMethod::RequestMethod(const char *method, const char* path) {
  buffer_size = strlen(method) + 1 + strlen(path) + 1 + strlen(HTTP1_VERSION_1_1 CRLF);
  buffer = new uint8_t[buffer_size+1];
  buffer[buffer_size] = '\0';  // null-terminate for debug print
  uint8_t* p = buffer;
  memcpy(p, method, strlen(method));
  p += strlen(method);
  *p++ = ' ';
  memcpy(p, path, strlen(path));
  p += strlen(path);
  *p++ = ' ';
  memcpy(p, HTTP1_VERSION_1_1 CRLF, strlen(HTTP1_VERSION_1_1 CRLF));
  p += strlen(HTTP1_VERSION_1_1 CRLF);
}

Http1Header::RequestMethod::~RequestMethod() {
  delete[] buffer;
}

Http1Header::ResonseStatus::ResonseStatus(int statusCode, const char* statusMessage): statusCode(statusCode), statusMessage(statusMessage) {
  buffer_size = strlen(HTTP1_VERSION_1_1) + 1 + 3 + 1 + strlen(statusMessage) + strlen(CRLF);
  buffer = new uint8_t[buffer_size+1];
  buffer[buffer_size] = '\0';  // null-terminate for debug print
  uint8_t* p = buffer;
  memcpy(p, HTTP1_VERSION_1_1, strlen(HTTP1_VERSION_1_1));
  p += strlen(HTTP1_VERSION_1_1);
  *p++ = ' ';
  sprintf((char*)p, "%03d", statusCode);
  p += 3;
  *p++ = ' ';
  memcpy(p, statusMessage, strlen(statusMessage));
  p += strlen(statusMessage);
  memcpy(p, CRLF, strlen(CRLF));
  p += strlen(CRLF);
}
Http1Header::ResonseStatus::~ResonseStatus() {
  delete[] buffer;
}
bool Http1Header::ResonseStatus::isValid() const {
  return buffer != nullptr;
}
Http1Header::ResonseStatus Http1Header::ResonseStatus::fromBytes(const uint8_t* data, std::size_t* size) {
  const char* p = (const char*)data;
  const char* end = p + *size;
  if ((end - p) < (int)(strlen(HTTP1_VERSION_1_1))) {
    // invalid HTTP response status line
    *size = 0;
    return ResonseStatus();
  }
  if(0!=strncmp(p, HTTP1_VERSION_1_1, strlen(HTTP1_VERSION_1_1)) && 0!=strncmp(p, HTTP1_VERSION_1_0, strlen(HTTP1_VERSION_1_0))) {
    // valid HTTP response status line
    *size = 0;
    return ResonseStatus();
  }
  p += strlen(HTTP1_VERSION_1_1);
  if ((end - p) < 1 || *p != ' ') {
    // invalid HTTP response status line
    *size = 0;
    return ResonseStatus();
  }
  p++; // skip space
  if ((end - p) < 3) {
    // invalid HTTP response status line
    *size = 0;
    return ResonseStatus();
  }
  char statusCodeStr[4];
  strncpy(statusCodeStr, p, 3);
  const int statusCode = std::atoi(statusCodeStr);
  p += 3; // skip status code
  if ((end - p) < 1 || *p != ' ') {
    // invalid HTTP response status line
    *size = 0;
    return ResonseStatus();
  }
  p++; // skip space
  bool foundCR = false;
  const char* statusMessageStart = p;
  while ((end - p) >= 1) {
    if (*p == '\r') {
      foundCR = true;
    } else if (foundCR && *p == '\n') {
      p++;
      *size = p - (const char*)data;
      const size_t statusMessageLength = p - statusMessageStart - 2; // exclude \r\n
      char statusMessage[statusMessageLength+1];
      statusMessage[statusMessageLength] = '\0';
      strncpy(statusMessage, statusMessageStart, statusMessageLength);
      return ResonseStatus(statusCode, statusMessageStart);  // ignore status message
    }
    p++;
  }
  *size = 0;
  return ResonseStatus();
};

Http1Header::HeaderField Http1Header::HeaderField::fromBytes(const uint8_t* data, std::size_t* size) {
  const char* p = (const char*)data;
  const char* end = p + *size;
  if ((end - p) >= 2 && *p == '\r' && *(p + 1) == '\n') {
    p += 2; // skip \r\n
    *size = p - (const char*)data;
    return HeaderField(EndOfHeaders);  // end of headers
  }
  const char* nameStart = p;
  while ((end - p) >= 1 && *p != ':') {
    p++;
  }
  if ((end - p) < 1 || *p != ':') {
    // invalid header field
    *size = 0;
    return HeaderField(Invalid);
  }
  const char* nameEnd = p;
  p++; // skip :
  if ((end - p) < 1 || *p != ' ') {
    // invalid header field
    *size = 0;
    return HeaderField(Invalid);
  }
  p++; // skip space
  const char* valueStart = p;
  while ((end - p) >= 1 && *p != '\r') {
    p++;
  }
  if ((end - p) < 2 || *p != '\r' || *(p + 1) != '\n') {
    // invalid header field
    *size = 0;
    return HeaderField(Invalid);
  }
  const char* valueEnd = p;
  p += 2; // skip \r\n
  *size = p - (const char*)data;
  String name;
  for (const char* q = nameStart; q < nameEnd; q++) {
    name += *q;
  }
  String value;
  for (const char* q = valueStart; q < valueEnd; q++) {
    value += *q;
  }
  return HeaderField(name.c_str(), value.c_str());
};

Http1Header::HeaderFieldPayload::HeaderFieldPayload(std::initializer_list<HeaderField> init) {
  buffer_size = 0;
  for (const auto& fragment : init) {
    buffer_size += fragment.name.length();
    buffer_size += 2; // for : and space
    buffer_size += fragment.value.length();
    buffer_size += 2; // for \r\n
  }
  buffer = new uint8_t[buffer_size];
  uint8_t* p = buffer;
  for (const auto& fragment : init) {
    log_d("Header: %s: %s", fragment.name.c_str(), fragment.value.c_str());
    memcpy(p, fragment.name.c_str(), fragment.name.length());
    p += fragment.name.length();
    *p++ = ':';
    *p++ = ' ';
    memcpy(p, fragment.value.c_str(), fragment.value.length());
    p += fragment.value.length();
    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
  }
}

Http1Header::HeaderFieldPayload::~HeaderFieldPayload() {
  delete[] buffer;
}

Http1Header::Http1Header(const RequestMethod& method, const HeaderFieldPayload& headerPayload):
 buffer_size(method.bytesSize()+headerPayload.bytesSize()+strlen(CRLF)),  // Request-Line + headers + final \r\n
 buffer(new uint8_t[buffer_size+1]) {
  buffer[buffer_size] = '\0';  // null-terminate for debug print
  uint8_t* p = buffer;
  memcpy(p, method.toBytes(), method.bytesSize());
  p += method.bytesSize();
  memcpy(p, headerPayload.toBytes(), headerPayload.bytesSize());
  p += headerPayload.bytesSize();
  memcpy(p, CRLF, strlen(CRLF));
  p += strlen(CRLF);
}

Http1Header::~Http1Header() {
  delete[] buffer;
}