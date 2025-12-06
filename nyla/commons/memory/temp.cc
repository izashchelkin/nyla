#include "nyla/commons/memory/temp.h"

#include <cstdlib>

#include "absl/log/check.h"

namespace nyla
{

namespace
{

struct TempArena
{
    char *base;
    char *at;
    size_t size;
};

} // namespace

static TempArena tempArena;

void TArenaInit()
{
    tempArena.size = 1 << 20;
    tempArena.base = (char *)malloc(tempArena.size);
    tempArena.at = tempArena.base;
}

auto TAlloc(size_t bytes, size_t align) -> void *
{
    CHECK_LT(bytes + align, tempArena.size);

    const size_t used = tempArena.at - tempArena.base;
    const size_t pad = (align - (used % align)) % align;
    if (used + pad + bytes > tempArena.size)
    {
        tempArena.at = tempArena.base;
        return TAlloc(bytes, align);
    }

    char *ret = tempArena.at + pad;
    tempArena.at += bytes + pad;
    return ret;
}

} // namespace nyla