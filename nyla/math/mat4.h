#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "nyla/math/vec3.h"

namespace nyla {

using Mat4Col = std::array<float, 4>;
using Mat4 = std::array<Mat4Col, 4>;

inline Mat4 Identity4 = {
    Mat4Col{1.0f, 0.0f, 0.0f, 0.0f},
    Mat4Col{0.0f, 1.0f, 0.0f, 0.0f},
    Mat4Col{0.0f, 0.0f, 1.0f, 0.0f},
    Mat4Col{0.0f, 0.0f, 0.0f, 1.0f},
};

inline Mat4 Mult(const Mat4& lhs, const Mat4& rhs) {
  Mat4 ret{};
  for (size_t i = 0; i < 4; ++i) {
    for (size_t j = 0; j < 4; ++j) {
      for (size_t k = 0; k < 4; ++k) {
        ret[j][i] += lhs[k][i] * rhs[j][k];
      }
    }
  }
  return ret;
}

inline Mat4 Translate(const Vec3& v) {
  Mat4 ret = Identity4;
  ret[3] = {v.x, v.y, v.z, 1.f};
  return ret;
}

inline Mat4 Rotate2D(float radians) {
  Mat4 ret = Identity4;
  ret[0] = {std::cos(radians), std::sin(radians), 0, 0};
  ret[1] = {-std::sin(radians), std::cos(radians), 0, 0};
  return ret;
}

inline Mat4 Ortho(float left, float right, float bottom, float top, float near,
                  float far) {
  return {Mat4Col{2.f / (right - left), 0.f, 0.f, 0.f},
          Mat4Col{0.f, 2.f / (top - bottom), 0.f, 0.f},
          Mat4Col{0.f, 0.f, 1.f / (far - near), 0.f},
          Mat4Col{
              -(right + left) / (right - left),
              -(top + bottom) / (top - bottom),
              -near / (far - near),
              1.f,
          }};
}

}  // namespace nyla
