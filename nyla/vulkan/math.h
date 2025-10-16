#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace nyla {

template <size_t N>
struct Vec {
  std::array<float, N> data;

  Vec() : data{} {};
  Vec(const Vec<N>& another) : data{another.data} {};

  Vec(float x, float y)
    requires(N == 2)
      : data{x, y} {}

  Vec(float x, float y, float z)
    requires(N == 3)
      : data{x, y, z} {}

  Vec(float x, float y, float z, float w)
    requires(N == 4)
      : data{x, y, z, w} {}

  Vec(Vec<3> v, float w)
    requires(N == 4)
      : data{v[0], v[1], v[2], w} {}

  float operator[](size_t i) const { return data[i]; }
  float& operator[](size_t i) { return data[i]; }

  Vec operator-() const {
    Vec<N> ret = *this;
    for (size_t i = 0; i < N; ++i) ret[i] = -ret[i];
    return ret;
  }

  void operator+=(const Vec<N>& rhs) {
    for (size_t i = 0; i < N; ++i) (*this)[i] += rhs[i];
  }

  Vec operator+(const Vec<N>& rhs) const {
    Vec ret = *this;
    ret += rhs;
    return ret;
  }

  void operator-=(const Vec<N>& rhs) {
    for (size_t i = 0; i < N; ++i) (*this)[i] -= rhs[i];
  }

  Vec operator-(const Vec<N>& rhs) const {
    Vec ret = *this;
    ret -= rhs;
    return ret;
  }

  void operator*=(float scalar) {
    for (size_t i = 0; i < N; ++i) (*this)[i] *= scalar;
  }

  Vec operator*(float scalar) const {
    Vec ret = *this;
    ret *= scalar;
    return ret;
  }

  void operator/=(float scalar) {
    for (size_t i = 0; i < N; ++i) (*this)[i] /= scalar;
  }

  Vec operator/(float scalar) const {
    Vec ret = *this;
    ret /= scalar;
    return ret;
  }
};

template <size_t N>
float Len(const Vec<N>& v) {
  float sum = 0.0f;
  for (size_t i = 0; i < N; ++i) sum += v.data[i] * v.data[i];
  return std::sqrt(sum);
}

template <size_t N>
Vec<N> Normalize(const Vec<N>& v) {
  float len = Len(v);
  return len ? v / len : v;
}

template <size_t N>
float Dot(const Vec<N>& lhs, const Vec<N>& rhs) {
  float sum = 0.0f;
  for (size_t i = 0; i < N; ++i) sum += lhs.data[i] * rhs.data[i];
  return sum;
}

using Vec2 = Vec<2>;
using Vec3 = Vec<3>;
using Vec4 = Vec<4>;
using Quat = Vec<4>;

inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs) {
  return {
      lhs[1] * rhs[2] - lhs[2] * rhs[1],
      lhs[2] * rhs[0] - lhs[0] * rhs[2],
      lhs[0] * rhs[1] - lhs[1] * rhs[0],
  };
}

template <size_t R, size_t C>
struct Mat {
  std::array<Vec<R>, C> cols;

  Mat() : cols{} {};
  Mat(const Mat<R, C>& another) : cols{another.cols} {};

  Mat(const Vec<R>& col1)
    requires(C == 1)
      : cols{col1} {}

  Mat(const Vec<R>& col1, const Vec<R>& col2, const Vec<R>& col3,
      const Vec<R>& col4)
    requires(C == 4)
      : cols{col1, col2, col3, col4} {}

  const Vec<R>& operator[](size_t i) const { return cols[i]; }
  Vec<R>& operator[](size_t i) { return cols[i]; }
};

template <size_t R, size_t S, size_t C>
Mat<R, C> Mult(const Mat<R, S>& lhs, const Mat<S, C>& rhs) {
  Mat<R, C> ret{};
  for (size_t i = 0; i < R; ++i) {
    for (size_t j = 0; j < C; ++j) {
      for (size_t k = 0; k < S; ++k) {
        ret[j][i] += lhs[k][i] * rhs[j][k];
      }
    }
  }
  return ret;
}

using Mat4x1 = Mat<4, 1>;
using Mat4 = Mat<4, 4>;

inline Mat4 Identity4 = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};

inline Mat4 RotationMatrix(const Quat& q) {
  const float x = q[0];
  const float y = q[1];
  const float z = q[2];
  const float w = q[3];

  // clang-format off
  return {
      { 1.f - 2.f*(y*y + z*z),  2.f*(x*y + w*z),        2.f*(x*z - w*y),        0.f },
      { 2.f*(x*y - w*z),        1.f - 2.f*(x*x + z*z),  2.f*(y*z + w*x),        0.f },
      { 2.f*(x*z + w*y),        2.f*(y*z - w*x),        1.f - 2.f*(x*x + y*y),  0.f },
      { 0.f,                    0.f,                    0.f,                    1.f },
  };
  // clang-format on
}

inline Mat4 LookAt(const Vec3& from, const Vec3& to, const Vec3& arbitrary_up) {
  const Vec3 forward = Normalize(from - to);
  const Vec3 right = Normalize(Cross(arbitrary_up, forward));
  const Vec3 up = Cross(forward, right);

  return {
      {right, 0.f},
      {up, 0.f},
      {forward, 0.f},
      {-Dot(right, from), -Dot(up, from), -Dot(forward, from), 1.f},
  };
}

inline Mat4 Perspective(float fovy_radians, float aspect, float z_near,
                        float z_far) {
  const float f = 1.f / std::tan(fovy_radians * .5f);
  const float nf = 1.f / (z_far - z_near);

  return {
      {f / aspect, 0.f, 0.f, 0.f},
      {0.f, f, 0.f, 0.f},
      {0.f, 0.f, z_far * nf, 1.f},
      {0.f, 0.f, -z_near * z_far * nf, 0.f},
  };
}

}  // namespace nyla
