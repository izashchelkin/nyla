#include "nyla/commons/mempage_pool.h"

#include "nyla/commons/array.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

namespace
{

struct mempage_pool
{
    uint8_t *begin;
    array<uint64_t, MemPagePool::kNumChunks / 64> bitset;
};
mempage_pool *manager;

} // namespace

namespace MemPagePool
{

void Bootstrap()
{
    manager = &RegionAlloc::Alloc<mempage_pool>(RegionAlloc::g_BootstrapAlloc);
    manager->begin = RegionAlloc::g_BootstrapAlloc.begin;
    manager->bitset[0] |= 1; // bootstrapAlloc owns first chunk
}

auto API AcquireChunk() -> span<uint8_t>
{
    for (uint64_t i = 0; i < Array::Size(manager->bitset); ++i)
    {
        uint64_t &qword = manager->bitset[i];

        if (qword == Limits<uint64_t>::Max())
            continue;

        uint32_t index = BitScanForward64(~qword);
        qword |= ((uint64_t)1) << index;

        return span<uint8_t>{
            manager->begin + (kChunkSize * ((i * 64) + index)),
            kChunkSize,
        };
    }

    ASSERT(false);
    return {};
}

void API ReleaseChunk(void *p)
{
    uint64_t ichunk = ((uint8_t *)p - manager->begin) / kChunkSize;
    uint64_t &qword = manager->bitset[ichunk / 64];

    uint64_t mask = ((uint64_t)1) << (ichunk % 64);
    DASSERT(qword & mask);

    DecommitMemPages(p, kChunkSize);

    qword &= ~(mask);
}

} // namespace MemPagePool

} // namespace nyla
