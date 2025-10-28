#include "Http2Frame.h"

Http2Frame::Http2Frame(Http2Frame::FrameType type, FrameFlags flags, StreamIdentifier streamId, std::size_t payloadLength, const uint8_t* payload):
 buffer_size(Http2Frame::Http2FrameHeaderSize + (payload?payloadLength:0)),
 buffer(new uint8_t[buffer_size]) {
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
  delete[] buffer;
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
  if (size < Http2Frame::Http2FrameHeaderSize + payloadLength) {
    return Http2Frame(type, flags, streamId, payloadLength, nullptr);
  }
  const uint8_t* payload = (0 < payloadLength)?(data + Http2Frame::Http2FrameHeaderSize):nullptr;
  return Http2Frame(type, flags, streamId, payloadLength, payload);
}
