#pragma once

#include "nyla/commons/assert.h"
#include <cstdint>
#include <limits>

namespace nyla
{

constexpr inline auto CastU32(uint64_t a) -> uint32_t
{
    NYLA_ASSERT(a <= std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(a);
}

constexpr inline auto CastI32(int64_t a) -> int32_t
{
    NYLA_ASSERT(a <= std::numeric_limits<int32_t>::max() && a >= std::numeric_limits<int32_t>::min());
    return static_cast<int32_t>(a);
}

} // namespace nyla
