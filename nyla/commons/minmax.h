#pragma once

namespace nyla
{

template <typename T> constexpr auto Min(const T &lhs, const T &rhs)
{
    return lhs < rhs ? lhs : rhs;
}

template <typename T> constexpr auto Max(const T &lhs, const T &rhs)
{
    return lhs > rhs ? lhs : rhs;
}

template <typename T> constexpr auto Clamp(const T &lhs, const T &min, const T &max)
{
    return lhs > max ? max : (lhs < min ? min : lhs);
}

} // namespace nyla
