#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"

namespace nyla
{

using float4 = array<float, 4>;
using float3 = array<float, 3>;
using float2 = array<float, 2>;

using uint4 = array<uint32_t, 4>;

using short2 = array<int16_t, 2>;

namespace Vec
{

template <typename T, uint64_t N>
[[nodiscard]]
auto LenSqr(const array<T, N> &self) -> T
{
    T sum{};
    for (uint32_t i = 0; i < N; ++i)
        sum += self[i] * self[i];
    return sum;
}

template <typename T, uint64_t N>
[[nodiscard]]
auto Len(const array<T, N> &self) -> T
{
    return Sqrt(LenSqr(self));
}

template <typename T, uint64_t N>
[[nodiscard]]
auto Normalized(const array<T, N> &self) -> array<T, N>
{
    return (self / Len(self));
}

template <typename T, uint64_t N>
[[nodiscard]]
constexpr auto Dot(const array<T, N> &lhs, const array<T, N> &rhs) -> T
{
    T sum{};
    for (uint32_t i = 0; i < N; ++i)
        sum += lhs[i] * rhs[i];
    return sum;
}

template <typename T>
[[nodiscard]]
constexpr auto Cross(const array<T, 3> &lhs, const array<T, 3> &rhs) -> array<T, 3>
{
    return {
        lhs[1] * rhs.m_Data[2] - lhs[2] * rhs[1],
        lhs[2] * rhs.m_Data[0] - lhs[0] * rhs[2],
        lhs[0] * rhs.m_Data[1] - lhs[1] * rhs[0],
    };
}

} // namespace Vec

} // namespace nyla