#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/limits.h"

namespace nyla
{

constexpr inline auto CastU32(uint64_t a) -> uint32_t
{
    ASSERT(a <= Limits<uint32_t>::Max());
    return static_cast<uint32_t>(a);
}

constexpr inline auto CastI32(int64_t a) -> int32_t
{
    ASSERT(a <= Limits<int32_t>::Max() && a >= Limits<int32_t>::Min());
    return static_cast<int32_t>(a);
}

constexpr inline auto CastI32(uint64_t a) -> int32_t
{
    ASSERT(a <= Limits<int32_t>::Max());
    return static_cast<int32_t>(a);
}

} // namespace nyla