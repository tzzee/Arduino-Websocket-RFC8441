#pragma once

#include <cstddef>
#include <new>

#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif

namespace ws_rfc8441 {

inline void* psramPreferredAlloc(std::size_t bytes) {
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

inline void psramPreferredFree(void* p) {
#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
  heap_caps_free(p);
#else
  ::operator delete(p);
#endif
}

/**
 * @brief std::map/std::queue等のノード確保をPSRAM優先にするSTL Allocator。
 * @tparam T 確保対象の要素型。
 */
template <typename T>
struct PsramStlAllocator {
  using value_type = T;

  PsramStlAllocator() noexcept = default;
  template <typename U>
  PsramStlAllocator(const PsramStlAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    void* p = psramPreferredAlloc(n * sizeof(T));
    if (p == nullptr) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(p);
  }

  void deallocate(T* p, std::size_t) noexcept {
    psramPreferredFree(p);
  }

  template <typename U>
  bool operator==(const PsramStlAllocator<U>&) const noexcept { return true; }
  template <typename U>
  bool operator!=(const PsramStlAllocator<U>&) const noexcept { return false; }
};

}  // namespace ws_rfc8441
