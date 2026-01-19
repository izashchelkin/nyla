#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/platform/platform.h"
#include <array>
#include <cstdint>

namespace nyla
{

#if 0
cmake --build build/linux-debug --target nyla_commons_memory_region_alloc_test && ctest --test-dir build/linux-debug region_alloc_test
#endif

void Test()
{
    RegionAlloc rootAlloc(g_Platform->ReserveMemPages(64_MiB), 0, 64_MiB, RegionAllocCommitPageGrowth::GetInstance());

    RegionAlloc scratch = rootAlloc.PushSubAlloc(1_MiB);
    {
        scratch.PushBytes(1_MiB, 1);
        scratch.PushBytes(1_MiB, 1);
    }
    rootAlloc.Pop(scratch.GetBase());

    const uint32_t startMemoryUsed = rootAlloc.GetBytesUsed();

    std::array<void *, 100> pushed;

    for (uint32_t i = 0; i < pushed.size(); ++i)
    {
        pushed[i] = rootAlloc.PushBytes(1_KiB, 1);
        *((char *)pushed[i]) = '\0';
    }

    for (uint32_t i = pushed.size(); i > 0; --i)
    {
        rootAlloc.Pop(pushed[i - 1]);
    }

    const uint32_t endMemoryUsed = rootAlloc.GetBytesUsed();
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