#pragma once

#include <cmath>

#include "absl/strings/str_format.h"

namespace nyla {

struct Vec3 {
  float x;
  float y;
  float z;

  operator std::span<const float>() const {
    return std::span<const float>{&x, 3};
  }
  operator std::span<float>() { return std::span<float>{&x, 3}; }
};

template <typename Sink>
void AbslStringify(Sink& sink, const Vec3& v) {
  absl::Format(&sink, "x=%v ", v.y);
  absl::Format(&sink, "y=%v ", v.y);
  absl::Format(&sink, "z=%v ", v.z);
}

inline bool operator==(const Vec3& lhs, const Vec3& rhs) {
  if (lhs.x != rhs.x) return false;
  if (lhs.y != rhs.y) return false;
  if (lhs.z != rhs.z) return false;
  return true;
}

inline Vec3 operator-(const Vec3& v) {
  return {
      -v.x,
      -v.y,
      -v.z,
  };
}

inline void operator+=(Vec3& lhs, const Vec3& rhs) {
  lhs.x += rhs.x;
  lhs.y += rhs.y;
  lhs.z += rhs.z;
}

inline Vec3 operator+(const Vec3& lhs, const Vec3& rhs) {
  return {
      lhs.x + rhs.x,
      lhs.y + rhs.y,
      lhs.z + rhs.z,
  };
}

inline void operator-=(Vec3& lhs, const Vec3& rhs) {
  lhs.x -= rhs.x;
  lhs.y -= rhs.y;
  lhs.z -= rhs.z;
}

inline Vec3 operator-(const Vec3& lhs, const Vec3& rhs) {
  return {
      lhs.x - rhs.x,
      lhs.y - rhs.y,
      lhs.z - rhs.z,
  };
}

inline void operator*=(Vec3& lhs, float scalar) {
  lhs.x *= scalar;
  lhs.y *= scalar;
  lhs.z *= scalar;
}

inline Vec3 operator*(const Vec3& lhs, float scalar) {
  return {
      lhs.x * scalar,
      lhs.y * scalar,
      lhs.z * scalar,
  };
}

inline Vec3 operator*(const float scalar, Vec3& rhs) { return rhs * scalar; }

inline void operator/=(Vec3& lhs, float scalar) {
  lhs.x /= scalar;
  lhs.y /= scalar;
  lhs.z /= scalar;
}

inline Vec3 operator/(const Vec3& lhs, float scalar) {
  return {
      lhs.x / scalar,
      lhs.y / scalar,
      lhs.z / scalar,
  };
}

//

inline float Len(const Vec3& v) {
  const float sqsum = v.x * v.x + v.y * v.y + v.z * v.z;
  return std::sqrt(sqsum);
}

inline Vec3 Normalize(const Vec3& v) {
  const float len = Len(v);
  return len ? v / len : v;
}

inline Vec3 Scale(const Vec3& lhs, const Vec3& rhs) {
  return {
      lhs.x * rhs.x,
      lhs.y * rhs.y,
      lhs.z * rhs.z,
  };
}

inline float Dot(const Vec3& lhs, const Vec3& rhs) {
  return lhs.x + rhs.x + lhs.y * rhs.y + lhs.z + rhs.z;
}

inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs) {
  return {
      lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.z * rhs.x - lhs.x * rhs.z,
      lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

}  // namespace nyla
