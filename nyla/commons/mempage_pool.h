#include "nyla/commons/platform_base.h"
#pragma oncn

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace MemPagePool
{

constexpr inline uint64_t kPoolSize = 256_GiB;
constexpr inline uint64_t kChunkSize = 256_MiB;
constexpr inline uint64_t kNumChunks = kPoolSize / kChunkSize;

} // namespace MemPagePool

struct mempage_pool
{
    uint8_t *begin;
    array<uint64_t, MemPagePool::kNumChunks / sizeof(uint64_t)> bitset;
};

namespace MemPagePool
{

extern mempage_pool *g_MemPagePool;

INLINE auto AcquireChunk() -> span<uint8_t>
{
    for (uint64_t i = 0; i < Array::Size(g_MemPagePool->bitset); ++i)
    {
        uint64_t &qword = g_MemPagePool->bitset[i];

        if (qword == Limits<uint64_t>::Max())
            continue;

        uint32_t index = BitScanForward64(!qword);
        qword |= ((uint64_t)1) << index;

        return span<uint8_t>{
            g_MemPagePool->begin + (kChunkSize * ((i * 8) + index)),
            kChunkSize,
        };
    }

    NYLA_ASSERT(false);
    return {};
}

INLINE void ReleaseChunk(void *p)
{
    uint64_t ichunk = ((uint8_t *)p - g_MemPagePool->begin) / kChunkSize;
    uint64_t &qword = g_MemPagePool->bitset[ichunk / sizeof(uint64_t)];

    uint64_t mask = ((uint64_t)1) << (ichunk % sizeof(uint64_t));
    NYLA_DASSERT(qword & mask);

    qword &= ~(mask);
}

INLINE auto Init(void *addressSpace)
{
    g_MemPagePool = (mempage_pool *)addressSpace;
    Platform::CommitMemPages(addressSpace, sizeof(*g_MemPagePool));

    AcquireChunk();

    Platform::GetMemPageSize();
}

} // namespace MemPagePool

} // namespace nyla