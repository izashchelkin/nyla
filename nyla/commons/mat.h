#pragma once

#include "nyla/commons/fmt.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/vec.h"

#include <cstdint>

namespace nyla
{

template <uint64_t N, uint64_t M, typename T> struct mat : array<array<T, M>, N>
{
    auto operator*(const mat &rhs) -> mat
    {
        mat ret{};
        for (uint64_t row = 0; row < N; ++row)
        {
            for (uint64_t col = 0; col < N; ++col)
            {
                for (uint64_t k = 0; k < N; ++k)
                    ret[col][row] += (*this)[k][row] * rhs[col][k];
            }
        }
        return ret;
    }
};

using float4x4 = mat<4, 4, float>;

namespace Mat
{

template <uint64_t N, uint64_t M, typename T> INLINE void Identity(mat<N, M, T> &out)
{
    MemZero(&out);

    for (uint32_t i = 0; i < N; ++i)
        out[i][i] = 1;
}

[[nodiscard]]
auto API Inverse(const mat<4, 4, float> &m) -> mat<4, 4, float>;

template <typename T, uint32_t N>
[[nodiscard]]
INLINE auto Translate(const array<T, N> &v) -> mat<N, N, T>
{
    mat<N, N, T> ret;
    Identity(ret);
    for (uint64_t i = 0; i < N; ++i)
        ret[N - 1][i] = v[i];
    return ret;
}

template <typename T, uint32_t N>
[[nodiscard]]
INLINE auto Scale(const array<T, N> &v) -> mat<N, N, T>
{
    mat<N, N, T> ret;
    Identity(ret);
    for (uint32_t i = 0; i < N; ++i)
        ret[i][i] = static_cast<T>(v[i]);
    return ret;
}

[[nodiscard]]
INLINE auto Rotate(float radians) -> mat<4, 4, float>
{
    mat<4, 4, float> ret;
    Identity(ret);

    const float c = Cos(radians);
    const float s = Sin(radians);

    ret[0][0] = c;
    ret[0][1] = s;
    ret[1][0] = -s;
    ret[1][1] = c;

    return ret;
}

} // namespace Mat

template <typename T, uint32_t N> class Mat
{
  public:
    Mat() = default;

    Mat(std::initializer_list<T> elems)
    {
        ASSERT(elems.Size() == static_cast<size_t>(N * N));
        auto it = elems.begin();
        for (uint32_t col = 0; col < N; ++col)
        {
            for (uint32_t row = 0; row < N; ++row, ++it)
                m_Data[col][row] = *it;
        }
    }

    Mat(std::initializer_list<Vec<T, N>> cols)
    {
        ASSERT(cols.Size() == static_cast<size_t>(N));
        auto it = cols.begin();
        for (uint32_t col = 0; col < N; ++col, ++it)
        {
            const Vec<T, N> &v = *it;
            for (uint32_t row = 0; row < N; ++row)
                m_Data[col][row] = v[row];
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
                m_Data[col][row] = static_cast<T>(v[col][row]);
        }
    }

    [[nodiscard]] auto operator[](uint32_t col) -> Array<T, N> &
    {
        ASSERT(col < N);
        return m_Data[col];
    }
    [[nodiscard]] auto operator[](uint32_t col) const -> const Array<T, N> &
    {
        ASSERT(col < N);
        return m_Data[col];
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

    [[nodiscard]]
    static auto Perspective(T fovRadians, T aspect, T nearPlane, T farPlane) -> Mat
        requires(N == 4 && std::floating_point<T>)
    {
        ASSERT(aspect != static_cast<T>(0));
        ASSERT(farPlane != nearPlane);

        const T halfFov = fovRadians * static_cast<T>(0.5);
        const T f = static_cast<T>(1) / std::tan(halfFov);
        const T fn = farPlane - nearPlane;

        // Column-major layout
        return {f / aspect,
                static_cast<T>(0),
                static_cast<T>(0),
                static_cast<T>(0),

                static_cast<T>(0),
                f,
                static_cast<T>(0),
                static_cast<T>(0),

                static_cast<T>(0),
                static_cast<T>(0),
                farPlane / fn,
                static_cast<T>(1), // This '1' copies the Z value into the W component

                static_cast<T>(0),
                static_cast<T>(0),
                -(farPlane * nearPlane) / fn,
                static_cast<T>(0)};
    }

    template <typename K>
        requires(N == 4 && std::convertible_to<K, T>)
    [[nodiscard]]
    static auto LookAt(const Vec<K, 3> &eye, const Vec<K, 3> &center, const Vec<K, 3> &up) -> Mat
    {
        // 1. Forward is unchanged
        const Vec<T, 3> f = (center - eye).Normalized();

        // 2. LHS Right Vector: Swap the cross product order to 'up x f'
        const Vec<T, 3> s = up.Cross(f).Normalized();

        // 3. LHS Up Vector: 's x f'
        const Vec<T, 3> u = s.Cross(f);

        // Column-major layout (Standard Left-Handed View Matrix)
        return {
            s[0],          u[0],          f[0],          static_cast<T>(0),

            s[1],          u[1],          f[1],          static_cast<T>(0),

            s[2],          u[2],          f[2],          static_cast<T>(0),

            -(s.Dot(eye)), -(u.Dot(eye)), -(f.Dot(eye)), static_cast<T>(1),
        };
    }

  private:
    Array<Array<T, N>, N> m_Data{};
};

using float4x4 = Mat<float, 4>;

} // namespace nyla