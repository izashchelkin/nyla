#pragma once

#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

namespace RegionAlloc
{

extern API region_alloc g_BootstrapAlloc;

[[nodiscard]]
INLINE auto Create(uint64_t maxSize, uint64_t precommitSize) -> region_alloc
{
    ASSERT(maxSize <= MemPagePool::kChunkSize);
    ASSERT(precommitSize <= maxSize);

    region_alloc ret{};
    ret.begin = MemPagePool::AcquireChunk().data;
    ret.end = ret.begin + maxSize;
    ret.at = ret.begin;
    ret.commitedEnd = ret.at + precommitSize;

    if (precommitSize > 0)
        CommitMemPages(ret.begin, precommitSize);

    return ret;
}

INLINE void Destroy(region_alloc &self)
{
    if (self.commitedEnd > self.begin)
        DecommitMemPages(self.begin, self.commitedEnd - self.begin);

    MemPagePool::ReleaseChunk(self.begin);
    MemZero(&self);
}

INLINE void Reset(region_alloc &self)
{
    self.at = self.begin;
}

INLINE void Reset(region_alloc &self, void *p)
{
    DASSERT(p != nullptr && p >= self.begin && p < self.end && p < self.commitedEnd);

    self.at = (uint8_t *)p;
}

template <typename T>
[[nodiscard]]
INLINE auto Alloc(region_alloc &self) -> T &
{
    uint8_t *mem = Alloc(self, sizeof(T), required_align_v<T>);
    return *((T *)mem);
}

[[nodiscard]]
INLINE auto Alloc(region_alloc &self, uint64_t size, uint64_t align) -> uint8_t *
{
    self.at = AlignedUp(self.at, Max(align, kMinAlign));
    uint8_t *const ret = self.at;
    self.at += size;

    ASSERT(self.at <= self.end);
    if (self.at > self.commitedEnd)
    {
        uint8_t *oldCommitedEnd;
        if (self.commitedEnd)
            oldCommitedEnd = self.commitedEnd;
        else
            oldCommitedEnd = self.begin;

        self.commitedEnd = AlignedUp(self.at, kPageSize);
        CommitMemPages(oldCommitedEnd, self.commitedEnd - oldCommitedEnd);
    }

    MemZero(ret, size);
    return ret;
}

template <typename T>
[[nodiscard]]
INLINE auto AllocArray(region_alloc &self, uint64_t n) -> span<T>
{
    uint8_t *mem = Alloc(self, sizeof(T) * n, required_align_v<T>);
    return span<T>{(T *)mem, n};
}

template <typename T>
[[nodiscard]]
INLINE auto AllocArray(region_alloc &self, span<T> data) -> span<T>
{
    span<T> mem = AllocArray(self, data.size);
    MemCpy(mem.data, data.data, data.size);
    return span<T>{mem.data, data.size};
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto AllocVec(region_alloc &self) -> inline_vec<T, Capacity> &
{
    return *Alloc<inline_vec<T, Capacity>>(self);
}

template <uint64_t Capacity>
[[nodiscard]]
INLINE auto AllocString(region_alloc &self) -> inline_string<Capacity> &
{
    return *Alloc<inline_string<Capacity>>(self);
}

template <uint64_t Capacity>
[[nodiscard]]
INLINE auto AllocString(region_alloc &self, byteview str) -> inline_string<Capacity> &
{
    auto &ret = AllocString<Capacity>(self);
    InlineString::AppendSuffix(ret, str);
    return ret;
}

template <uint64_t Capacity>
[[nodiscard]]
INLINE auto SafeCStr(region_alloc &self, byteview str) -> const char *
{
    auto &ret = AllocString<Capacity>(self);
    InlineString::AppendSuffix(ret, str);
    return Span::CStr((byteview)ret);
}

#if 0
[[nodiscard]]
INLINE auto SubAlloc(region_alloc &self, uint64_t size) -> region_alloc
{
    ASSERT(size > 0);
    uint8_t *mem = Alloc(self, size, Platform::kPageSize);
    return region_alloc{
        .at = mem,
        .begin = mem,
        .end = mem + size,
        .commitedEnd = mem + size,
    };
}
#endif

} // namespace RegionAlloc
} // namespace nyla

#define SCRATCH_REMEMBER void *scratchResetMark = scratch.at
#define SCRATCH_RESET RegionAlloc::Reset(scratch, scratchResetMark)