#pragma once

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/macros.h"

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

extern region_alloc g_BootstrapAlloc;

[[nodiscard]]
INLINE auto Create(uint64_t maxSize, uint64_t precommitSize) -> region_alloc;

INLINE void Destroy(region_alloc &self);

INLINE void Reset(region_alloc &self)
{
    self.at = self.begin;
}

INLINE void Reset(region_alloc &self, void *p);

[[nodiscard]]
INLINE auto Alloc(region_alloc &self, uint64_t size, uint64_t align) -> uint8_t *;

template <typename T>
[[nodiscard]]
INLINE auto Alloc(region_alloc &self) -> T &
{
    uint8_t *mem = Alloc(self, sizeof(T), required_align_v<T>);
    return *((T *)mem);
}

} // namespace RegionAlloc
} // namespace nyla

#define SCRATCH_REMEMBER void *scratchResetMark = scratch.at
#define SCRATCH_RESET RegionAlloc::Reset(scratch, scratchResetMark)