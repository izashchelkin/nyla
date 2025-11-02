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

#if 0
template <typename T, typename... Args>
T* Tnew(Args&&... args) {
  void* p = BumpAlloc(sizeof(T));
  return new (p) T(std::forward<Args>(args)...);
}
#else
template <typename T, typename... Args>
T* Tnew(Args&&... args) {
  void* p = BumpAlloc(sizeof(T), alignof(T));
  return new (p) T{std::forward<Args>(args)...};
}
#endif

template <typename T>
T* Tnew(T&& value) {
  using U = std::remove_cvref_t<T>;
  void* p = BumpAlloc(sizeof(U), alignof(U));
  return new (p) U(value);
}

}  // namespace nyla
