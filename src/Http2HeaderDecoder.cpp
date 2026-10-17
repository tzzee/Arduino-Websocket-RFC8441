#include "Http2HeaderDecoder.h"

#include <Arduino.h>
#include <cstring>

#include <nghttp2/nghttp2.h>

#include <esp_heap_caps.h>

namespace {

// nghttp2 は realloc も使うので、差し替えのある ws_rfc8441_malloc ではなく heap_caps で揃える(PSRAM 優先)
void* hdMalloc(size_t size, void*) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void hdFree(void* ptr, void*) {
  heap_caps_free(ptr);
}

void* hdCalloc(size_t nmemb, size_t size, void*) {
  void* p = heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(nmemb, size, MALLOC_CAP_8BIT);
}

void* hdRealloc(void* ptr, size_t size, void*) {
  void* p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

nghttp2_mem kHdMem = {nullptr, hdMalloc, hdFree, hdCalloc, hdRealloc};

}  // namespace

Http2HeaderDecoder::~Http2HeaderDecoder() {
  reset();
}

void Http2HeaderDecoder::reset() {
  if (inflater_) {
    nghttp2_hd_inflate_del(inflater_);
    inflater_ = nullptr;
  }
}

bool Http2HeaderDecoder::decode(const std::uint8_t* block, std::size_t length, bool endHeaders, FieldFn fn,
                                void* arg) {
  if (!inflater_ && nghttp2_hd_inflate_new2(&inflater_, &kHdMem) != 0) {
    inflater_ = nullptr;
    log_e("Http2HeaderDecoder: inflater alloc failed");
    return false;
  }
  const std::uint8_t* in = block;
  std::size_t in_length = length;
  while (true) {
    nghttp2_nv nv;
    int flags = 0;
    const ssize_t used = nghttp2_hd_inflate_hd2(inflater_, &nv, &flags, in, in_length, endHeaders ? 1 : 0);
    if (used < 0) {
      log_e("Http2HeaderDecoder: HPACK decode failed rv=%d", static_cast<int>(used));
      reset();
      return false;
    }
    in += used;
    in_length -= static_cast<std::size_t>(used);
    if (flags & NGHTTP2_HD_INFLATE_EMIT) {
      fn(reinterpret_cast<const char*>(nv.name), nv.namelen, reinterpret_cast<const char*>(nv.value), nv.valuelen,
         arg);
    }
    if (flags & NGHTTP2_HD_INFLATE_FINAL) {
      nghttp2_hd_inflate_end_headers(inflater_);
      return true;
    }
    if ((flags & NGHTTP2_HD_INFLATE_EMIT) == 0 && in_length == 0) {
      return true;  // 続きは CONTINUATION で来る
    }
  }
}
