#pragma once

#include <cstddef>
#include <utility>

namespace nyla {

void TNewInit();
void* TNewAlloc(size_t bytes, size_t align);

template <typename T, typename... Args>
T* Tnew(Args&&... args) {
  void* p = TNewAlloc(sizeof(T), alignof(T));
  return ::new (p) T(std::forward<Args>(args)...);
}

template <class T>
auto Tnew_from(T&& value) -> std::remove_cvref_t<T>* {
  using U = std::remove_cvref_t<T>;
  return Tnew<U>(std::forward<T>(value));
}

}  // namespace nyla
