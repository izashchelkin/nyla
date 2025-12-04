#pragma once

#include <array>
#include <span>

namespace nyla
{

using Mat4Col = std::array<float, 4>;
using Mat4 = std::array<Mat4Col, 4>;

inline Mat4 Identity4 = {
    Mat4Col{1.0f, 0.0f, 0.0f, 0.0f},
    Mat4Col{0.0f, 1.0f, 0.0f, 0.0f},
    Mat4Col{0.0f, 0.0f, 1.0f, 0.0f},
    Mat4Col{0.0f, 0.0f, 0.0f, 1.0f},
};

Mat4 Mult(const Mat4 &lhs, const Mat4 &rhs);
Mat4 Inverse(const Mat4 &m);
Mat4 Translate(std::span<const float> v);
Mat4 ScaleXY(float x, float y);
Mat4 Rotate2D(float radians);
Mat4 Ortho(float left, float right, float top, float bottom, float near, float far);

} // namespace nyla