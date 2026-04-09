#pragma once

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform_base.h"

namespace nyla
{

struct region_alloc
{
    uint8_t *at;
    uint8_t *begin;
    uint8_t *end;
    uint8_t *commitedEnd;
};

namespace RegionAlloc
{

INLINE void Reset(region_alloc &self)
{
    self.at = self.begin;
}

INLINE void Reset(region_alloc &self, void *p)
{
    NYLA_DASSERT(p != nullptr && p >= self.begin && p < self.end && p < self.commitedEnd);

    self.at = (uint8_t *)p;
}

[[nodiscard]]
INLINE auto Alloc(region_alloc &self, uint64_t size, uint64_t align) -> uint8_t *
{
    self.at = AlignedUp(self.at, Max(align, kMinAlign));
    uint8_t *const ret = self.at;
    self.at += size;

    NYLA_ASSERT(self.at <= self.end);
    if (self.at > self.commitedEnd)
    {
        uint8_t *oldCommitedEnd;
        if (self.commitedEnd)
            oldCommitedEnd = self.commitedEnd;
        else
            oldCommitedEnd = self.begin;

        self.commitedEnd = AlignedUp(self.at, Platform::GetMemPageSize());
        Platform::CommitMemPages(oldCommitedEnd, self.commitedEnd - oldCommitedEnd);
    }

    MemZero(ret, size);
    return ret;
}

template <Plain T>
[[nodiscard]]
INLINE auto Alloc(region_alloc &self) -> T &
{
    uint8_t *mem = Alloc(self, sizeof(T), required_align_v<T>);
    return *((T *)mem);
}

template <Plain T>
[[nodiscard]]
INLINE auto AllocArray(region_alloc &self, uint64_t n) -> span<T>
{
    uint8_t *mem = Alloc(self, sizeof(T) * n, required_align_v<T>);
    return span<T>{(T *)mem, n};
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

[[nodiscard]]
INLINE auto PushSubAlloc(region_alloc &self, uint64_t size) -> region_alloc
{
    NYLA_ASSERT(size > 0);
    uint8_t *mem = Alloc(self, size, Platform::GetMemPageSize());
    return region_alloc{
        .at = mem,
        .begin = mem,
        .end = mem + size,
        .commitedEnd = mem + size,
    };
}

INLINE auto VoidScope(region_alloc &self, uint64_t size, auto &&fn) -> void
{
    auto mark = self.at;
    auto alloc = PushSubAlloc(self, size);
    fn(alloc);
    Reset(self, mark);
}

[[nodiscard]]
INLINE auto Scope(region_alloc &self, uint64_t size, auto &&fn)
{
    auto mark = self.at;
    auto alloc = PushSubAlloc(self, size);
    auto &&ret = fn(alloc);
    Reset(self, mark);
}

} // namespace RegionAlloc

} // namespace nyla