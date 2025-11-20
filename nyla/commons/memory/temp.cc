#include "nyla/commons/memory/temp.h"

#include <cstdlib>

#include "absl/log/check.h"

namespace nyla {

namespace {

struct TempArena {
  char* base;
  char* at;
  size_t size;
};

}  // namespace

static TempArena temp_arena;

void TArenaInit() {
  temp_arena.size = 1 << 20;
  temp_arena.base = (char*)malloc(temp_arena.size);
  temp_arena.at = temp_arena.base;
}

void* TAlloc(size_t bytes, size_t align) {
  CHECK_LT(bytes + align, temp_arena.size);

  const size_t used = temp_arena.at - temp_arena.base;
  const size_t pad = (align - (used % align)) % align;
  if (used + pad + bytes > temp_arena.size) {
    temp_arena.at = temp_arena.base;
    return TAlloc(bytes, align);
  }

  char* ret = temp_arena.at + pad;
  temp_arena.at += bytes + pad;
  return ret;
}

}  // namespace nyla