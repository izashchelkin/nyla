#pragma once

#include "nyla/math/mat4.h"
#include "nyla/math/vec3.h"

namespace nyla {

struct Quat {
  float w;
  float i;
  float j;
  float k;
};

inline Quat operator-(const Quat& v) { return {-v.w, -v.i, -v.j, -v.k}; }

inline Quat operator+(const Quat& lhs, const Quat& rhs) {
  return {lhs.w + rhs.w, lhs.i + rhs.i, lhs.j + rhs.j, lhs.k + rhs.k};
}

inline Quat operator*(const Quat& lhs, float scalar) {
  return {lhs.w * scalar, lhs.i * scalar, lhs.j * scalar, lhs.k * scalar};
}

inline Quat operator*(const float scalar, Quat& rhs) { return rhs * scalar; }

inline Quat operator/(const Quat& lhs, float scalar) {
  return {lhs.w / scalar, lhs.i / scalar, lhs.j / scalar, lhs.k / scalar};
}

//

inline float Len(const Quat& q) {
  return std::sqrt(q.w * q.w + q.i * q.i + q.j * q.j + q.k * q.k);
}

inline Quat Normalize(const Quat& v) {
  const float len = Len(v);
  return len ? v / len : v;
}

inline float Dot(const Quat& lhs, const Quat& rhs) {
  return lhs.w * rhs.w + lhs.i * rhs.i + lhs.j * rhs.j + lhs.k * rhs.k;
}

inline Quat MakeQuat(const Vec3& axis, float radians) {
  const Vec3 v = Normalize(axis) * sinf(radians / 2.f);
  return Quat{cosf(radians / 2.f), v.x, v.y, v.z};
}

inline Mat4 ToRotationMatrix(const Quat& q) {
  return {
      MatCol4{
          1 - 2 * (q.j * q.j + q.k * q.k),
          2 * (q.i * q.j - q.k * q.w),
          2 * (q.i * q.k + q.j * q.w),
          0,
      },
      MatCol4{
          2 * (q.i * q.j + q.k * q.w),
          1 - 2 * (q.i * q.i + q.k * q.k),
          2 * (q.j * q.k - q.i * q.w),
          0,
      },
      MatCol4{
          2 * (q.i * q.k - q.j * q.w),
          2 * (q.j * q.k + q.i * q.w),
          1 - 2 * (q.i * q.i + q.j * q.j),
          0,
      },
      MatCol4{0, 0, 0, 1},
  };
}

inline Quat Slerp(const Quat& q1, Quat q2, float t) {
  float dot = Dot(q1, q2);
  if (dot < 0.0f) {
    q2 = -q2;
    dot = -dot;
  }
  if (dot > 1.f - 1e-6f) {
    return Normalize(q1 * (1 - t) + q2 * t);
  }

  const float theta = acosf(dot);
  const float sinTheta = sinf(theta);

  const float w1 = sinf((1 - t) * theta) / sinTheta;
  const float w2 = sinf(t * theta) / sinTheta;

  return q1 * w1 + q2 * w2;
}

}  // namespace nyla
