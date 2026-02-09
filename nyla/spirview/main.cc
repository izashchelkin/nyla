#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/platform/platform.h"
#include <cstdint>

namespace nyla
{

auto PlatformMain() -> int
{
    RegionAllocator alloc(64_KiB, 64_KiB);

    NYLA_LOG("memory used=%d", alloc.GetBytesUsed());

    uint32_t *a = (uint32_t *)alloc.PushBytes(32_KiB, alignof(uint32_t));
    *a = 42;

    NYLA_LOG("memory used=%d", alloc.GetBytesUsed());

    uint32_t *b = (uint32_t *)alloc.PushBytes(32_KiB - sizeof(RegionAllocator::Block), alignof(uint32_t));
    *b = 30;

    NYLA_LOG("memory used=%d", alloc.GetBytesUsed());

    alloc.Pop(b);
    alloc.Pop(a);

    NYLA_LOG("memory used=%d", alloc.GetBytesUsed());

    return 0;
}

} // namespace nyla