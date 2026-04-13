#pragma once

#include <concepts>
#include <cstdint>

namespace nyla
{

struct handle
{
    uint32_t gen;
    uint32_t index;
};

template <typename T>
concept is_handle = std::derived_from<T, handle> && (sizeof(T) == sizeof(handle));

inline auto HandleIsSet(handle handle) -> bool
{
    return handle.gen;
}

inline auto operator==(const handle &lhs, const handle &rhs) -> bool
{
    return lhs.gen == rhs.gen && lhs.index == rhs.index;
}

template <class T>
    requires(std::derived_from<T, handle> && !std::same_as<T, handle>)
inline auto operator==(const T &lhs, const T &rhs) -> bool
{
    return static_cast<const handle &>(lhs) == static_cast<const handle &>(rhs);
}

} // namespace nyla