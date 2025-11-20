#pragma once

#include <cstddef>
#include <span>
#include <utility>

namespace nyla {

void TArenaInit();
void* TAlloc(size_t bytes, size_t align);

template <typename T, typename... Args>
T& Tnew(Args&&... args) {
  void* p = TAlloc(sizeof(T), alignof(T));
  return *(::new (p) T(std::forward<Args>(args)...));
}

template <class T>
std::remove_cvref_t<T>& Tmake(T&& value) {
  using U = std::remove_cvref_t<T>;
  return Tnew<U>(std::forward<T>(value));
}

template <class T>
std::span<T> Tarr(size_t n) {
  T* p = reinterpret_cast<T*>(TAlloc(sizeof(T) * n, alignof(T)));
  for (size_t i = 0; i < n; ++i) {
    ::new (p + i) T;
  }
  return std::span{p, n};
}

}  // namespace nyla