#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/platform/platform.h"
#include <array>
#include <cstdint>

namespace nyla
{

void Test()
{
    RegionAllocator alloc(8_MiB);

    const uint32_t startMemoryUsed = alloc.GetBytesUsed();

    std::array<void *, 100> pushed;

    for (uint32_t i = 0; i < pushed.size(); ++i)
    {
        pushed[i] = alloc.PushBytes(1_KiB, 1);
        *((char *)pushed[i]) = '\0';
    }

    for (uint32_t i = pushed.size(); i > 0; --i)
    {
        alloc.Pop(pushed[i - 1]);
    }

    const uint32_t endMemoryUsed = alloc.GetBytesUsed();
    NYLA_ASSERT(startMemoryUsed == endMemoryUsed);
}

auto PlatformMain() -> int
{
    g_Platform->Init({});

    Test();

    NYLA_LOG("");
    NYLA_LOG("==========================");
    NYLA_LOG("TESTS SUCCEEDED!");
    NYLA_LOG("==========================");
    NYLA_LOG("");

    return 0;
}

} // namespace nyla
