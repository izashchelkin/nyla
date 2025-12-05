#pragma once

#include "absl/log/check.h"
#include "nyla/commons/memory/temp.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace nyla
{

template <typename T, uint32_t N> struct VecSpan
{
    uint32_t size;
    std::array<T, N> data;
};

template <typename T, uint32_t N> class Vec
{
  public:
    Vec(std::initializer_list<T> elems) : m_data{elems}
    {
    }

    const T *data() const
    {
        return m_data.data();
    }

    const T &operator[](size_t i) const
    {
        CHECK_LT(i, N);
        return m_data[i];
    }

    T &operator[](size_t i)
    {
        CHECK_LT(i, N);
        return m_data[i];
    }

    bool operator==(const Vec &rhs) const
    {
        for (uint32_t i = 0; i < N; ++i)
        {
            if (m_data[i] != rhs.m_data[i])
                return false;
        }
        return true;
    }

    T Len() const
    {
        T sum{};
        for (uint32_t i = 0; i < N; ++i)
            sum += m_data[i] * m_data[i];
        return sum;
    }

    //

    Vec &operator+=(const Vec &rhs)
    {
        for (uint32_t i = 0; i < N; ++i)
            m_data[i] += rhs.m_data[i];
        return *this;
    }

    Vec operator+(const Vec &rhs) const
    {
        Vec lhs = *this;
        return (lhs += rhs);
    }

    Vec &operator-=(const Vec &rhs)
    {
        for (uint32_t i = 0; i < N; ++i)
            m_data[i] -= rhs.m_data[i];
        return *this;
    }

    Vec operator-(const Vec &rhs) const
    {
        Vec lhs = *this;
        return (lhs -= rhs);
    }

    Vec &operator*=(auto scalar)
    {
        for (uint32_t i = 0; i < N; ++i)
            m_data[i] *= static_cast<T>(scalar);
    }

    Vec operator*(auto scalar) const
    {
        Vec lhs = *this;
        return (lhs *= scalar);
    }

    Vec &operator/=(auto scalar)
    {
        for (uint32_t i = 0; i < N; ++i)
            m_data[i] /= static_cast<T>(scalar);
    }

    Vec operator/(auto scalar) const
    {
        Vec lhs = *this;
        return (lhs /= scalar);
    }

    //

    Vec &Normalize()
    {
        return (*this /= Len());
    }

    Vec Normalized() const
    {
        return (*this / Len());
    }

  private:
    std::array<T, N> m_data;
};

using float4 = Vec<float, 4>;
using float3 = Vec<float, 3>;
using float2 = Vec<float, 2>;
using floatN = VecSpan<float, 4>;

} // namespace nyla
