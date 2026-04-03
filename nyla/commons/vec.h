#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/complex.h"
#include "nyla/commons/minmax.h"

namespace nyla
{

template <typename T, uint32_t N> class Vec
{
  public:
    constexpr Vec() = default;

    template <typename K, uint32_t M>
        requires std::convertible_to<K, T>
    constexpr explicit Vec(const Vec<K, M> &v)
    {
        auto count = Min(N, M);
        for (uint32_t i = 0; i < count; ++i)
            m_Data[i] = static_cast<T>(v[i]);
    }

    constexpr explicit Vec(T x)
        requires(N == 1)
        : m_Data{x}
    {
    }

    constexpr Vec(T x, T y)
        requires(N == 2)
        : m_Data{x, y}
    {
    }

    constexpr Vec(Complex<T> c)
        requires(N == 2)
        : m_Data{c.Real(), c.Imag()}
    {
    }

    constexpr Vec(T x, T y, T z)
        requires(N == 3)
        : m_Data{x, y, z}
    {
    }

    constexpr Vec(T x, T y, T z, T w)
        requires(N == 4)
        : m_Data{x, y, z, w}
    {
    }

    constexpr explicit operator Complex<T>() const
        requires(N == 2)
    {
        return Complex{m_Data[1], m_Data[0]};
    }

    [[nodiscard]] constexpr auto data() const -> const T *
    {
        return m_Data.Data();
    }

    [[nodiscard]]
    constexpr auto operator[](size_t i) const -> const T &
    {
        return m_Data[i];
    }

    [[nodiscard]]
    constexpr auto operator[](size_t i) -> T &
    {
        return m_Data[i];
    }

    [[nodiscard]]
    constexpr auto operator-() const -> Vec
    {
        Vec ret;
        for (uint32_t i = 0; i < N; ++i)
            ret.m_Data[i] = -m_Data[i];
        return ret;
    }

    [[nodiscard]]
    constexpr auto operator==(const Vec &rhs) const -> bool
    {
        for (uint32_t i = 0; i < N; ++i)
        {
            if (m_Data[i] != rhs.m_Data[i])
                return false;
        }
        return true;
    }

    [[nodiscard]]
    constexpr auto LenSqr() const -> T
    {
        T sum{};
        for (uint32_t i = 0; i < N; ++i)
            sum += m_Data[i] * m_Data[i];
        return sum;
    }

    [[nodiscard]]
    constexpr auto Len() const -> T
    {
        return Sqrt(LenSqr());
    }

    //

    constexpr auto operator+=(const Vec &rhs) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] += rhs.m_Data[i];
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator+(const Vec &rhs) const -> Vec
    {
        Vec lhs = *this;
        return (lhs += rhs);
    }
    constexpr auto operator-=(const Vec &rhs) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] -= rhs.m_Data[i];
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator-(const Vec &rhs) const -> Vec
    {
        Vec lhs = *this;
        return (lhs -= rhs);
    }

    constexpr auto operator*=(auto scalar) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] *= static_cast<T>(scalar);
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator*(auto scalar) const -> Vec
    {
        Vec lhs = *this;
        return (lhs *= scalar);
    }

    constexpr auto operator/=(auto scalar) -> Vec &
    {
        for (uint32_t i = 0; i < N; ++i)
            m_Data[i] /= static_cast<T>(scalar);
        return *this;
    }

    [[nodiscard]]
    constexpr auto operator/(auto scalar) const -> Vec
    {
        Vec lhs = *this;
        return (lhs /= scalar);
    }

    //

    constexpr auto Normalize()
    {
        *this /= Len();
    }

    [[nodiscard]]
    constexpr auto Normalized() const -> Vec
    {
        return (*this / Len());
    }

    [[nodiscard]]
    constexpr auto Dot(const Vec &rhs) const -> T
    {
        T sum{};
        for (uint32_t i = 0; i < N; ++i)
            sum += m_Data[i] * rhs.m_Data[i];
        return sum;
    }

    [[nodiscard]]
    constexpr auto Cross(const Vec &rhs) const -> Vec
        requires(N == 3)
    {
        return Vec{
            m_Data[1] * rhs.m_Data[2] - m_Data[2] * rhs[1],
            m_Data[2] * rhs.m_Data[0] - m_Data[0] * rhs[2],
            m_Data[0] * rhs.m_Data[1] - m_Data[1] * rhs[0],
        };
    }

  private:
    Array<T, N> m_Data;
};

using float4 = Vec<float, 4>;
using float3 = Vec<float, 3>;
using float2 = Vec<float, 2>;

using uint4 = Vec<uint32_t, 4>;

using short2 = Vec<int16_t, 2>;

} // namespace nyla