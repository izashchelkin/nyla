#include "nyla/commons/dev_log.h"

#include <cstdint>

#include "nyla/commons/inline_string.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform_mutex.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span.h"
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

dev_log_state *g_dev_log;

} // namespace

namespace DevLog
{

void API Bootstrap()
{
    g_dev_log = &RegionAlloc::Alloc<dev_log_state>(RegionAlloc::g_BootstrapAlloc);
    g_dev_log->mutex = PlatformMutex::Create(RegionAlloc::g_BootstrapAlloc);
    g_dev_log->head = 0;
    g_dev_log->count = 0;
}

void API Push(byteview line)
{
    if (!g_dev_log)
        return;

    uint64_t n = Min<uint64_t>(line.size, kLineCap);

    PlatformMutex::Lock(*g_dev_log->mutex);
    auto &slot = g_dev_log->ring[g_dev_log->head];
    if (n)
        MemCpy(slot.data.data, line.data, n);
    slot.size = n;
    g_dev_log->head = (g_dev_log->head + 1) % kRingSize;
    if (g_dev_log->count < kRingSize)
        ++g_dev_log->count;
    PlatformMutex::Unlock(*g_dev_log->mutex);
}

auto API Snapshot(region_alloc &alloc, uint32_t maxLines) -> span<byteview>
{
    if (!g_dev_log)
        return span<byteview>{};

    PlatformMutex::Lock(*g_dev_log->mutex);

    uint32_t n = Min<uint32_t>(maxLines, g_dev_log->count);
    span<byteview> out = RegionAlloc::AllocArray<byteview>(alloc, n);

    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t idx = (g_dev_log->head + kRingSize - 1 - i) % kRingSize;
        const auto &slot = g_dev_log->ring[idx];
        span<uint8_t> bytes = RegionAlloc::AllocArray<uint8_t>(alloc, slot.size);
        if (slot.size)
            MemCpy(bytes.data, slot.data.data, slot.size);
        out.data[i] = byteview{bytes.data, slot.size};
    }

    PlatformMutex::Unlock(*g_dev_log->mutex);
    return out;
}

} // namespace DevLog

} // namespace nyla
