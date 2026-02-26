#pragma once

#include "nyla/commons/assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace nyla
{

template <typename T, uint32_t N> class Vec
{
  public:
    constexpr Vec() = default;

    constexpr Vec(std::initializer_list<T> elems)
    {
        NYLA_ASSERT(elems.size() <= N);
        std::ranges::copy(elems.begin(), elems.end(), m_Data.begin());
    }

    template <typename K, uint32_t M>
        requires std::convertible_to<K, T>
    explicit Vec(const Vec<K, M> &v)
    {
        auto count = std::min<uint32_t>(N, M);
        for (uint32_t i = 0; i < count; ++i)
            m_Data[i] = static_cast<T>(v[i]);
    }

    explicit Vec(std::complex<T> c)
        requires(N == 2)
        : m_Data{c.imag(), c.real()}
    {
    }

    explicit operator std::complex<T>() const
        requires(N == 2)
    {
        return std::complex{m_Data[1], m_Data[0]};
    }

    [[nodiscard]] auto data() const -> const T *
    {
        return m_Data.data();
    }

    [[nodiscard]]
    auto operator[](size_t i) const -> const T &
    {
        NYLA_ASSERT(i < N);
        return m_Data[i];
    }

    [[nodiscard]]
    auto operator[](size_t i) -> T &
    {
        NYLA_ASSERT(i < N);
        return m_Data[i];
    }

    [[nodiscard]]
    auto operator-() const -> Vec
    {
        Vec ret;
        for (uint32_t i = 0; i < N; ++i)
            ret.m_Data[i] = -m_Data[i];
        return ret;
    }

    [[nodiscard]]
    auto operator==(const Vec &rhs) const -> bool
    {
        for (uint32_t i = 0; i < N; ++i)
        {
            if (m_Data[i] != rhs.m_Data[i])
                return false;
        }
        return true;
    }

    [[nodiscard]]
    auto LenSqr() const -> T
    {
        T sum{};
        for (uint32_t i = 0; i < N; ++i)
            sum += m_Data[i] * m_Data[i];
        return sum;
    }

    [[nodiscard]]
    auto Len() const -> T
    {
        return std::sqrt(LenSqr());
    }

    //

    auto operator+=(const Vec &rhs) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] += rhs.m_Data[i];
        return *this;
    }

    [[nodiscard]]
    auto operator+(const Vec &rhs) const -> Vec
    {
        Vec lhs = *this;
        return (lhs += rhs);
    }

    auto operator-=(const Vec &rhs) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] -= rhs.m_Data[i];
        return *this;
    }

    [[nodiscard]]
    auto operator-(const Vec &rhs) const -> Vec
    {
        Vec lhs = *this;
        return (lhs -= rhs);
    }

    auto operator*=(auto scalar) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] *= static_cast<T>(scalar);
        return *this;
    }

    [[nodiscard]]
    auto operator*(auto scalar) const -> Vec
    {
        Vec lhs = *this;
        return (lhs *= scalar);
    }

    auto operator/=(auto scalar) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] /= static_cast<T>(scalar);
        return *this;
    }

    [[nodiscard]]
    auto operator/(auto scalar) const -> Vec
    {
        Vec lhs = *this;
        return (lhs /= scalar);
    }

    //

    auto Normalize()
    {
        *this /= Len();
    }

    [[nodiscard]]
    auto Normalized() const -> Vec
    {
        return (*this / Len());
    }

    [[nodiscard]]
    auto Dot(const Vec &rhs) const -> T
    {
        T sum{};
        for (uint32_t i = 0; i < N; ++i)
            sum += m_Data[i] * rhs.m_Data[i];
        return sum;
    }

    [[nodiscard]]
    auto Cross(const Vec &rhs) const -> Vec
        requires(N == 3)
    {
        return Vec{
            m_Data[1] * rhs.m_Data[2] - m_Data[2] * rhs[1],
            m_Data[2] * rhs.m_Data[0] - m_Data[0] * rhs[2],
            m_Data[0] * rhs.m_Data[1] - m_Data[1] * rhs[0],
        };
    }

  private:
    std::array<T, N> m_Data;
};

using float4 = Vec<float, 4>;
using float3 = Vec<float, 3>;
using float2 = Vec<float, 2>;

using uint4 = Vec<uint32_t, 4>;

using short2 = Vec<int16_t, 2>;

} // namespace nyla