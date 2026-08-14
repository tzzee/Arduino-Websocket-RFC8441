#include "WsMemory.h"

#include <new>

#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif

namespace {

void* (*g_malloc)(size_t) = nullptr;
void (*g_free)(void*) = nullptr;

void* defaultMalloc(size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p != nullptr) {
    return p;
  }
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#else
  return ::operator new(bytes, std::nothrow);
#endif
}

void defaultFree(void* p) {
#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
  heap_caps_free(p);
#else
  ::operator delete(p);
#endif
}

}  // namespace

void ws_rfc8441_set_malloc_free(void* (*malloc_fn)(size_t), void (*free_fn)(void*)) {
  g_malloc = malloc_fn;
  g_free = free_fn;
}

void* ws_rfc8441_malloc(size_t bytes) {
  return (g_malloc != nullptr) ? g_malloc(bytes) : defaultMalloc(bytes);
}

void ws_rfc8441_free(void* p) {
  if (p == nullptr) {
    return;
  }
  if (g_free != nullptr) {
    g_free(p);
  } else {
    defaultFree(p);
  }
}
