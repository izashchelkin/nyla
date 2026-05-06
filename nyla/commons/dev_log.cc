#include "nyla/commons/dev_log.h"

#include <cstdint>

#include "nyla/commons/inline_string.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform_mutex.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

constexpr inline uint64_t kLineCap = 128;
constexpr inline uint32_t kRingSize = 32;

struct dev_log_state
{
    platform_mutex *mutex;
    inline_string<kLineCap> ring[kRingSize];
    uint32_t head;
    uint32_t count;
};

dev_log_state *manager;

} // namespace

namespace DevLog
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<dev_log_state>(RegionAlloc::g_BootstrapAlloc);
    manager->mutex = PlatformMutex::Create(RegionAlloc::g_BootstrapAlloc);
    manager->head = 0;
    manager->count = 0;
}

void API Push(byteview line)
{
    if (!manager)
        return;

    PlatformMutex::Lock(*manager->mutex);
    auto &slot = manager->ring[manager->head];
    InlineString::Assign(slot, line);
    manager->head = (manager->head + 1) % kRingSize;
    if (manager->count < kRingSize)
        ++manager->count;
    PlatformMutex::Unlock(*manager->mutex);
}

auto API Snapshot(region_alloc &alloc, uint32_t maxLines) -> span<byteview>
{
    if (!manager)
        return span<byteview>{};

    PlatformMutex::Lock(*manager->mutex);

    uint32_t n = Min<uint32_t>(maxLines, manager->count);
    span<byteview> out = RegionAlloc::AllocArray<byteview>(alloc, n);

    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t idx = (manager->head + kRingSize - 1 - i) % kRingSize;
        out.data[i] = RegionAlloc::CopyByteView(alloc, manager->ring[idx]);
    }

    PlatformMutex::Unlock(*manager->mutex);
    return out;
}

} // namespace DevLog

} // namespace nyla
