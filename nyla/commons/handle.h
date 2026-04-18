#pragma once

#include <concepts>
#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"

namespace nyla
{

struct handle
{
    uint32_t gen;
    uint32_t index;

    operator bool()
    {
        return !MemEq(this, 0, sizeof(handle));
    }
};

template <typename T>
concept is_handle = std::derived_from<T, handle> && (sizeof(T) == sizeof(handle));

INLINE auto operator==(const handle &lhs, const handle &rhs) -> bool
{
    return lhs.gen == rhs.gen && lhs.index == rhs.index;
}

template <class T>
    requires(std::derived_from<T, handle> && !std::same_as<T, handle>)
INLINE auto operator==(const T &lhs, const T &rhs) -> bool
{
    return static_cast<const handle &>(lhs) == static_cast<const handle &>(rhs);
}

} // namespace nyla