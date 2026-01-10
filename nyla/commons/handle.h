#pragma once

#include <concepts>
#include <cstdint>

namespace nyla
{

struct Handle
{
    uint32_t gen{};
    uint32_t index{};
};

inline auto HandleIsSet(Handle handle) -> bool
{
    return handle.gen;
}

inline auto operator==(const Handle &lhs, const Handle &rhs) -> bool
{
    return lhs.gen == rhs.gen && lhs.index == rhs.index;
}

template <class T>
    requires(std::derived_from<T, Handle> && !std::same_as<T, Handle>)
inline auto operator==(const T &lhs, const T &rhs) -> bool
{
    return static_cast<const Handle &>(lhs) == static_cast<const Handle &>(rhs);
}

} // namespace nyla