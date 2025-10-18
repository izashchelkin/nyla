#pragma once

#include <array>
#include <cstddef>

namespace nyla {

using MatCol4 = std::array<float, 4>;
using Mat4 = std::array<MatCol4, 4>;

inline Mat4 Identity4 = {
    MatCol4{1.0f, 0.0f, 0.0f, 0.0f},
    MatCol4{0.0f, 1.0f, 0.0f, 0.0f},
    MatCol4{0.0f, 0.0f, 1.0f, 0.0f},
    MatCol4{0.0f, 0.0f, 0.0f, 1.0f},
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

}  // namespace nyla
