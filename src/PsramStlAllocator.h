#pragma once

#include <cstddef>
#include <new>

#include "WsMemory.h"

namespace ws_rfc8441 {

/**
 * @brief std::map/std::queue等のノード確保をws_rfc8441_malloc/freeに委ねるSTL Allocator。
 * @tparam T 確保対象の要素型。
 */
template <typename T>
struct PsramStlAllocator {
  using value_type = T;

  PsramStlAllocator() noexcept = default;
  template <typename U>
  PsramStlAllocator(const PsramStlAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    void* p = ws_rfc8441_malloc(n * sizeof(T));
    if (p == nullptr) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(p);
  }

  void deallocate(T* p, std::size_t) noexcept {
    ws_rfc8441_free(p);
  }

  template <typename U>
  bool operator==(const PsramStlAllocator<U>&) const noexcept { return true; }
  template <typename U>
  bool operator!=(const PsramStlAllocator<U>&) const noexcept { return false; }
};

}  // namespace ws_rfc8441
