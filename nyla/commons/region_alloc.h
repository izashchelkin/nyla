#pragma once

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/fmt.h"
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
    uint8_t *reservedEnd;
    uint8_t *commitedEnd;
};

namespace RegionAlloc
{

void NYLA_API CommitPages(region_alloc &self);

[[nodiscard]]
INLINE auto PushBytes(region_alloc &self, uint64_t size, uint64_t align) -> uint8_t *
{
    self.at = AlignedUp(self.at, Max(align, kMinAlign));
    uint8_t *const ret = self.at;
    self.at += size;

    NYLA_ASSERT(self.at <= self.reservedEnd);
    if (self.at > self.commitedEnd)
        CommitPages(self);

    MemZero(ret, size);
    return ret;
}

INLINE void Reset(region_alloc &self)
{
    self.at = self.begin;
}

INLINE void Reset(region_alloc &self, void *p)
{
    NYLA_DASSERT(p != nullptr);
    NYLA_DASSERT(p >= self.begin);
    NYLA_DASSERT(p < self.commitedEnd);

    self.at = (uint8_t *)p;
}

template <Plain T>
[[nodiscard]]
INLINE auto Push(region_alloc &self) -> T &
{
    uint8_t *mem = PushBytes(self, sizeof(T), required_align_v<T>);
    return *((T *)mem);
}

template <Plain T>
[[nodiscard]]
INLINE auto PushN(region_alloc &self, uint64_t n) -> span<T>
{
    uint8_t *mem = PushBytes(self, sizeof(T) * n, required_align_v<T>);
    return span<T>{(T *)mem, n};
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto PushVec(region_alloc &self) -> inline_vec<T, Capacity> &
{
    return *Push<inline_vec<T, Capacity>>(self);
}

template <uint64_t Capacity>
[[nodiscard]]
INLINE auto PushString(region_alloc &self) -> inline_string<Capacity> &
{
    return *Push<inline_string<Capacity>>(self);
}

[[nodiscard]]
INLINE auto PushSubAlloc(region_alloc &self, uint64_t size) -> region_alloc
{
    NYLA_ASSERT(size > 0);
    uint8_t *mem = PushBytes(self, size, Platform::GetMemPageSize());
    return region_alloc{
        .at = mem,
        .begin = mem,
        .reservedEnd = mem + size,
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
    return ret;
}

} // namespace RegionAlloc

} // namespace nyla