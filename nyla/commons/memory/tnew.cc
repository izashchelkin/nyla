#include "nyla/commons/memory/tnew.h"

#include <cstdlib>

#include "absl/log/check.h"

namespace nyla {

struct TNewArena {
  char* base;
  char* at;
  size_t size;
};
static TNewArena tnew_arena;

void TNewInit() {
  tnew_arena.size = 1 << 20;
  tnew_arena.base = (char*)malloc(tnew_arena.size);
  tnew_arena.at = tnew_arena.base;
}

void* TNewAlloc(size_t bytes, size_t align) {
  CHECK_LT(bytes + align, tnew_arena.size);

  const size_t used = tnew_arena.at - tnew_arena.base;
  const size_t pad = (align - (used % align)) % align;
  if (used + pad + bytes > tnew_arena.size) {
    tnew_arena.at = tnew_arena.base;
    return TNewAlloc(bytes, align);
  }

  char* ret = tnew_arena.at + pad;
  tnew_arena.at += bytes + pad;
  return ret;
}

}  // namespace nyla
