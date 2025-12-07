#include "nyla/engine0/frame_arena.h"

#include <cstdint>
#include <cstdlib>

#include "absl/log/check.h"

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
    CHECK_LT(used + pad + bytes, frameArena.size);

    char *ret = frameArena.at + pad;
    frameArena.at += bytes + pad;
    return ret;
}

} // namespace nyla::engine0_internal