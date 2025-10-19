#pragma once

#include <cmath>

namespace nyla {

struct Vec3 {
  float x;
  float y;
  float z;
};

inline Vec3 operator-(const Vec3& v) { return {-v.x, -v.y, -v.z}; }

inline void operator+=(Vec3& lhs, const Vec3& rhs) {
  lhs.x += rhs.x;
  lhs.y += rhs.y;
  lhs.z += rhs.z;
}

inline Vec3 operator+(const Vec3& lhs, const Vec3& rhs) {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline void operator-=(Vec3& lhs, const Vec3& rhs) {
  lhs.x -= rhs.x;
  lhs.y -= rhs.y;
  lhs.z -= rhs.z;
}

inline Vec3 operator-(const Vec3& lhs, const Vec3& rhs) {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline void operator*=(Vec3& lhs, float scalar) {
  lhs.x *= scalar;
  lhs.y *= scalar;
  lhs.z *= scalar;
}

inline Vec3 operator*(const Vec3& lhs, float scalar) {
  return {lhs.x * scalar, lhs.y * scalar, lhs.z * scalar};
}

inline Vec3 operator*(const float scalar, Vec3& rhs) { return rhs * scalar; }

inline void operator/=(Vec3& lhs, float scalar) {
  lhs.x /= scalar;
  lhs.y /= scalar;
  lhs.z /= scalar;
}

inline Vec3 operator/(const Vec3& lhs, float scalar) {
  return {lhs.x / scalar, lhs.y / scalar, lhs.z / scalar};
}

//

inline float Len(const Vec3& v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3 Normalize(const Vec3& v) {
  const float len = Len(v);
  return len ? v / len : v;
}

inline float Dot(const Vec3& lhs, const Vec3& rhs) {
  return lhs.x + rhs.x + lhs.y * rhs.y + lhs.z + rhs.z;
}

inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs) {
  return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

}  // namespace nyla
