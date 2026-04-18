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

[[nodiscard]]
INLINE auto Translate(const array<float, 4> &v) -> mat<4, 4, float>
{
    mat<4, 4, float> ret;
    Identity(ret);
    for (uint64_t i = 0; i < 4; ++i)
        ret[3][i] = v[i];
    return ret;
}

[[nodiscard]]
INLINE auto Scale(const array<float, 4> &v) -> mat<4, 4, float>
{
    mat<4, 4, float> ret;
    Identity(ret);
    for (uint32_t i = 0; i < 4; ++i)
        ret[i][i] = v[i];
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

[[nodiscard]]
INLINE auto Ortho(float left, float right, float top, float bottom, float near, float far) -> mat<4, 4, float>
{
    const float two = 2.f;
    const float rl = (right - left);
    const float tb = (top - bottom);
    const float fn = (far - near);

    return mat<4, 4, float>{
        two / rl,
        0.f,
        0.f,
        0.f,
        0.f,
        two / tb,
        0.f,
        0.f,
        0.f,
        0.f,
        1.f / fn,
        0.f,
        -(right + left) / rl,
        -(top + bottom) / tb,
        -near / fn,
        1.f,
    };
}

[[nodiscard]]
INLINE auto Perspective(float fovRadians, float aspect, float nearPlane, float farPlane) -> mat<4, 4, float>
{
    DASSERT(aspect != 0.f);
    DASSERT(farPlane != nearPlane);

    const float halfFov = fovRadians * .5f;
    const float f = 1.f / Tan(halfFov);
    const float fn = farPlane - nearPlane;

    return {
        f / aspect,
        0.f,
        0.f,
        0.f,
        //
        0.f,
        f,
        0.f,
        0.f,
        //
        0.f,
        0.f,
        farPlane / fn,
        1.f,
        //
        0.f,
        0.f,
        -(farPlane * nearPlane) / fn,
        0.f,
    };
}

[[nodiscard]]
INLINE auto LookAt(const array<float, 3> &eye, const array<float, 3> &center, const array<float, 3> &up)
    -> mat<4, 4, float>
{
    array<float, 3> f = Vec::Normalized(center - eye);
    array<float, 3> s = Vec::Normalized(Vec::Cross(up, f));
    array<float, 3> u = Vec::Cross(s, f);

    return {
        s[0],
        u[0],
        f[0],
        0.f,

        s[1],
        u[1],
        f[1],
        0.f,

        s[2],
        u[2],
        f[2],
        0.f,

        -(Vec::Dot(s, eye)),
        -(Vec::Dot(u, eye)),
        -(Vec::Dot(f, eye)),
        1.f,
    };
}

} // namespace Mat

} // namespace nyla