#pragma once

#include <cstddef>
#include <utility>

namespace nyla {

struct BumpAllocArena {
  char* base;
  char* at;
  size_t size;
};

extern BumpAllocArena global_tnew_arena;
void InitGlobalTnew();

void* BumpAlloc(size_t bytes, size_t align);
void BumpAllocReset();

template <typename T, typename... Args>
T* Tnew(Args&&... args) {
  void* p = BumpAlloc(sizeof(T), alignof(T));
  return ::new (p) T(std::forward<Args>(args)...);
}

template <class T>
auto Tnew_from(T&& value) -> std::remove_cvref_t<T>* {
  using U = std::remove_cvref_t<T>;
  return Tnew<U>(std::forward<T>(value));
}

}  // namespace nyla
