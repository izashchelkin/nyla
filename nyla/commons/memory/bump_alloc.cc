#include "nyla/commons/memory/bump_alloc.h"

#include <cstdlib>

#include "absl/log/check.h"

namespace nyla {

BumpAllocArena global_tnew_arena;

void InitGlobalTnew() {
  global_tnew_arena.size = 1 << 20;
  global_tnew_arena.base = (char*)malloc(global_tnew_arena.size);
  global_tnew_arena.at = global_tnew_arena.base;
}

void* BumpAlloc(size_t bytes, size_t align) {
  const size_t used = global_tnew_arena.at - global_tnew_arena.base;
  const size_t pad = (align - (used % align)) % align;
  CHECK_LE(used + pad + bytes, global_tnew_arena.size)
      << used << " " << pad << " " << bytes;

  char* ret = global_tnew_arena.at + pad;
  global_tnew_arena.at += bytes + pad;
  return ret;
}

void BumpAllocReset() { global_tnew_arena.at = global_tnew_arena.base; }

}  // namespace nyla
