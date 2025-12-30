#pragma once

#include "nyla/commons/assert.h"
#include "nyla/commons/math/vec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>

namespace nyla
{

template <typename T, uint32_t N> class Mat
{
  public:
    Mat() = default;

    Mat(std::initializer_list<T> elems)
    {
        NYLA_ASSERT(elems.size() == static_cast<size_t>(N * N));
        auto it = elems.begin();
        for (uint32_t col = 0; col < N; ++col)
        {
            for (uint32_t row = 0; row < N; ++row, ++it)
                m_data[col][row] = *it;
        }
    }

    Mat(std::initializer_list<Vec<T, N>> cols)
    {
        NYLA_ASSERT(cols.size() == static_cast<size_t>(N));
        auto it = cols.begin();
        for (uint32_t col = 0; col < N; ++col, ++it)
        {
            const Vec<T, N> &v = *it;
            for (uint32_t row = 0; row < N; ++row)
                m_data[col][row] = v[row];
        }
    }

    template <typename K, uint32_t M>
        requires std::convertible_to<K, T>
    explicit Mat(const Mat<K, M> &v)
    {
        const auto count = std::min<uint32_t>(N, M);
        for (uint32_t col = 0; col < count; ++col)
        {
            for (uint32_t row = 0; row < count; ++row)
                m_data[col][row] = static_cast<T>(v[col][row]);
        }
    }

    [[nodiscard]]
    static auto Identity() -> Mat
    {
        Mat ret{};
        for (uint32_t i = 0; i < N; ++i)
            ret[i][i] = static_cast<T>(1);
        return ret;
    }

    [[nodiscard]] auto operator[](uint32_t col) -> std::array<T, N> &
    {
        NYLA_ASSERT(col < N);
        return m_data[col];
    }
    [[nodiscard]] auto operator[](uint32_t col) const -> const std::array<T, N> &
    {
        NYLA_ASSERT(col < N);
        return m_data[col];
    }

    // Column-major matrix multiply: this * rhs
    [[nodiscard]]
    auto Mult(const Mat &rhs) const -> Mat
    {
        Mat ret{}; // zero-init
        for (uint32_t row = 0; row < N; ++row)
        {
            for (uint32_t col = 0; col < N; ++col)
            {
                for (uint32_t k = 0; k < N; ++k)
                    ret[col][row] += m_data[k][row] * rhs.m_data[col][k];
            }
        }
        return ret;
    }

    [[nodiscard]]
    auto Inversed() const -> Mat
        requires std::floating_point<T>
    {
        std::array<std::array<T, 2 * N>, N> a{};

        for (uint32_t row = 0; row < N; ++row)
        {
            for (uint32_t col = 0; col < N; ++col)
                a[row][col] = m_data[col][row];

            for (uint32_t col = 0; col < N; ++col)
                a[row][N + col] = (row == col) ? static_cast<T>(1) : static_cast<T>(0);
        }

        for (uint32_t col = 0; col < N; ++col)
        {
            // Pivot selection
            uint32_t pivotRow = col;
            T maxAbs = std::fabs(a[pivotRow][col]);
            for (uint32_t row = col + 1; row < N; ++row)
            {
                T v = std::fabs(a[row][col]);
                if (v > maxAbs)
                {
                    maxAbs = v;
                    pivotRow = row;
                }
            }

            NYLA_ASSERT(maxAbs > static_cast<T>(1e-8));

            if (pivotRow != col)
            {
                for (uint32_t j = 0; j < 2 * N; ++j)
                    std::swap(a[col][j], a[pivotRow][j]);
            }

            const T pivot = a[col][col];
            for (uint32_t j = 0; j < 2 * N; ++j)
                a[col][j] /= pivot;

            for (uint32_t row = 0; row < N; ++row)
            {
                if (row == col)
                    continue;

                const T factor = a[row][col];
                if (factor != static_cast<T>(0))
                {
                    for (uint32_t j = 0; j < 2 * N; ++j)
                        a[row][j] -= factor * a[col][j];
                }
            }
        }

        Mat inv{};
        for (uint32_t row = 0; row < N; ++row)
        {
            for (uint32_t col = 0; col < N; ++col)
                inv.m_data[col][row] = a[row][N + col];
        }

        return inv;
    }

    template <typename K, uint32_t M>
        requires std::convertible_to<K, T>
    [[nodiscard]]
    static auto Translate(const Vec<K, M> &v) -> Mat
    {
        Mat ret = Mat::Identity();
        const auto count = std::min<uint32_t>(N, M);
        for (uint32_t i = 0; i < count; ++i)
            ret[N - 1][i] = static_cast<T>(v[i]);
        return ret;
    }

    template <typename K, uint32_t M>
        requires std::convertible_to<K, T>
    [[nodiscard]]
    static auto Scale(const Vec<K, M> &v) -> Mat
    {
        Mat ret = Mat::Identity();
        const auto count = std::min<uint32_t>(N, M);
        for (uint32_t i = 0; i < count; ++i)
            ret[i][i] = static_cast<T>(v[i]);
        return ret;
    }

    [[nodiscard]]
    static auto Rotate(T radians) -> Mat
        requires(N == 4 && std::floating_point<T>)
    {
        Mat ret = Mat::Identity();
        const T c = std::cos(radians);
        const T s = std::sin(radians);

        ret[0][0] = c;
        ret[0][1] = s;
        ret[1][0] = -s;
        ret[1][1] = c;

        return ret;
    }

    [[nodiscard]]
    static auto Ortho(T left, T right, T top, T bottom, T near, T far) -> Mat
        requires(N == 4 && std::floating_point<T>)
    {
        const T two = static_cast<T>(2);
        const T rl = (right - left);
        const T tb = (top - bottom);
        const T fn = (far - near);

        return {
            two / rl,
            static_cast<T>(0),
            static_cast<T>(0),
            static_cast<T>(0),
            static_cast<T>(0),
            two / tb,
            static_cast<T>(0),
            static_cast<T>(0),
            static_cast<T>(0),
            static_cast<T>(0),
            static_cast<T>(1) / fn,
            static_cast<T>(0),
            -(right + left) / rl,
            -(top + bottom) / tb,
            -near / fn,
            static_cast<T>(1),
        };
    }

  private:
    std::array<std::array<T, N>, N> m_data{};
};

using float4x4 = Mat<float, 4>;

} // namespace nyla