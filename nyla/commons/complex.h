#pragma once

#include <concepts>

#include "nyla/commons/math.h"

namespace nyla
{

template <std::floating_point T> struct Complex
{
    constexpr Complex(T r = 0, T i = 0) : m_Real(r), m_Imag(i)
    {
    }

    auto Real() -> T
    {
        return m_Real;
    }

    auto Imag() -> T
    {
        return m_Imag;
    }

    constexpr auto operator+(const Complex &other) const -> Complex
    {
        return {m_Real + other.m_Real, m_Imag + other.m_Imag};
    }

    constexpr auto operator-(const Complex &other) const -> Complex
    {
        return {m_Real - other.m_Real, m_Imag - other.m_Imag};
    }

    constexpr auto operator*(const Complex &other) const -> Complex
    {
        return {(m_Real * other.m_Real) - (m_Imag * other.m_Imag), (m_Real * other.m_Imag) + (m_Imag * other.m_Real)};
    }

    constexpr auto operator/(const Complex &other) const -> Complex
    {
        T denom = (other.m_Real * other.m_Real) + (other.m_Imag * other.m_Imag);
        return {((m_Real * other.m_Real) + (m_Imag * other.m_Imag)) / denom,
                ((m_Imag * other.m_Real) - (m_Real * other.m_Imag)) / denom};
    }

    constexpr auto operator+=(const Complex &other) -> Complex &
    {
        m_Real += other.m_Real;
        m_Imag += other.m_Imag;
        return *this;
    }

    constexpr auto Conj() const -> Complex
    {
        return {m_Real, -m_Imag};
    }

    auto Norm() const -> T
    {
        return (m_Real * m_Real) + (m_Imag * m_Imag);
    }

    auto Abs() const -> T
    {
        return Sqrt(Norm());
    }

  private:
    T m_Real;
    T m_Imag;
};

using Complex64 = Complex<double>;
using Complex32 = Complex<float>;

} // namespace nyla