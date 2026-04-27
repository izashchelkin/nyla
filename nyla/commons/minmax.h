#pragma once

namespace nyla
{

template <typename T> constexpr auto Min(const T &lhs, const T &rhs)
{
    if (lhs < rhs)
        return lhs;
    else
        return rhs;
}

template <typename T> constexpr auto Max(const T &lhs, const T &rhs)
{
    if (lhs > rhs)
        return lhs;
    else
        return rhs;
}

template <typename T> constexpr auto Clamp(const T &lhs, const T &min, const T &max)
{
    if (lhs > max)
        return max;
    else if (lhs < min)
        return min;
    else
        return lhs;
}

} // namespace nyla