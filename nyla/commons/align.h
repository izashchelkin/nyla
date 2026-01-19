#pragma once

namespace nyla
{

template <typename T> inline constexpr auto AlignedUp(T n, T align) -> T
{
    return (n + align - 1) & ~(align - 1);
}

template <typename T> inline constexpr void AlignUp(T &n, T align)
{
    n = AlignedUp(n, align);
}

} // namespace nyla