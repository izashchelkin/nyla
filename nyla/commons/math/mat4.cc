#include "nyla/commons/math/mat4.h"

#include <array>
#include <cmath>

#include "absl/log/check.h"

namespace nyla
{

auto Mult(const Mat4 &lhs, const Mat4 &rhs) -> Mat4
{
    Mat4 ret{};
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            for (size_t k = 0; k < 4; ++k)
            {
                ret[j][i] += lhs[k][i] * rhs[j][k];
            }
        }
    }
    return ret;
}

auto Inverse(const Mat4 &m) -> Mat4
{
    float a[4][8];

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            a[row][col] = m[col][row];
        }

        for (int col = 0; col < 4; ++col)
        {
            a[row][4 + col] = (row == col) ? 1.0f : 0.0f;
        }
    }

    for (int col = 0; col < 4; ++col)
    {
        int pivot_row = col;
        float max_abs = std::fabs(a[pivot_row][col]);
        for (int row = col + 1; row < 4; ++row)
        {
            float v = std::fabs(a[row][col]);
            if (v > max_abs)
            {
                max_abs = v;
                pivot_row = row;
            }
        }

        CHECK(max_abs > 1e-8f) << "Mat4 is singular or nearly singular in Inverse()";

        if (pivot_row != col)
        {
            for (int j = 0; j < 8; ++j)
            {
                std::swap(a[col][j], a[pivot_row][j]);
            }
        }

        float pivot = a[col][col];
        for (int j = 0; j < 8; ++j)
        {
            a[col][j] /= pivot;
        }

        for (int row = 0; row < 4; ++row)
        {
            if (row == col)
                continue;

            float factor = a[row][col];
            if (factor != 0.0f)
            {
                for (int j = 0; j < 8; ++j)
                {
                    a[row][j] -= factor * a[col][j];
                }
            }
        }
    }

    Mat4 inv{};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            inv[col][row] = a[row][4 + col];
        }
    }

    return inv;
}

auto Translate(std::span<const float> v) -> Mat4
{
    CHECK(v.size() >= 2);

    Mat4 ret = Identity4;
    ret[3] = {v[0], v[1], v.size() > 2 ? v[2] : 0.f, 1.f};
    return ret;
}

auto ScaleXY(float x, float y) -> Mat4
{
    Mat4 ret = Identity4;
    ret[0][0] = x;
    ret[1][1] = y;
    return ret;
}

auto Rotate2D(float radians) -> Mat4
{
    Mat4 ret = Identity4;
    ret[0] = {std::cos(radians), std::sin(radians), 0, 0};
    ret[1] = {-std::sin(radians), std::cos(radians), 0, 0};
    return ret;
}

auto Ortho(float left, float right, float top, float bottom, float near, float far) -> Mat4
{
    return {
        Mat4Col{2.f / (right - left), 0.f, 0.f, 0.f},
        Mat4Col{0.f, 2.f / (top - bottom), 0.f, 0.f},
        Mat4Col{0.f, 0.f, 1.f / (far - near), 0.f},
        Mat4Col{
            -(right + left) / (right - left),
            -(top + bottom) / (top - bottom),
            -near / (far - near),
            1.f,
        },
    };
}

} // namespace nyla