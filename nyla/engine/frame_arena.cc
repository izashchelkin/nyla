#include "nyla/engine/frame_arena.h"

#include "nyla/commons/assert.h"
#include <cstdint>
#include <cstdlib>

namespace nyla::engine0_internal
{

namespace
{

struct FrameArena
{
    char *base;
    char *at;
    size_t size;
};

} // namespace

static FrameArena frameArena;

void FrameArenaInit()
{
    frameArena.size = 1 << 20;
    frameArena.base = (char *)malloc(frameArena.size);
    frameArena.at = frameArena.base;
}

auto FrameArenaAlloc(uint32_t bytes, uint32_t align) -> char *
{
    const size_t used = frameArena.at - frameArena.base;
    const size_t pad = (align - (used % align)) % align;
    NYLA_ASSERT(used + pad + bytes < frameArena.size);

    char *ret = frameArena.at + pad;
    frameArena.at += bytes + pad;
    return ret;
}

} // namespace nyla::engine0_internal