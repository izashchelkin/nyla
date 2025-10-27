#pragma once

#include <cmath>
#include <span>

#include "absl/strings/str_format.h"

namespace nyla {

struct Vec2 {
  float x;
  float y;

  operator std::span<const float>() const {
    return std::span<const float>{&x, 2};
  }
  operator std::span<float>() { return std::span<float>{&x, 2}; }
};

template <typename Sink>
void AbslStringify(Sink& sink, const Vec2& v) {
  absl::Format(&sink, "x=%v ", v.x);
  absl::Format(&sink, "y=%v ", v.y);
}

inline bool operator==(const Vec2& lhs, const Vec2& rhs) {
  if (lhs.x != rhs.x) return false;
  if (lhs.y != rhs.y) return false;
  return true;
}

inline Vec2 operator-(const Vec2& v) {
  return {
      -v.x,
      -v.y,
  };
}

inline void operator+=(Vec2& lhs, const Vec2& rhs) {
  lhs.x += rhs.x;
  lhs.y += rhs.y;
}

inline Vec2 operator+(const Vec2& lhs, const Vec2& rhs) {
  return {
      lhs.x + rhs.x,
      lhs.y + rhs.y,
  };
}

inline void operator-=(Vec2& lhs, const Vec2& rhs) {
  lhs.x -= rhs.x;
  lhs.y -= rhs.y;
}

inline Vec2 operator-(const Vec2& lhs, const Vec2& rhs) {
  return {
      lhs.x - rhs.x,
      lhs.y - rhs.y,
  };
}

inline void operator*=(Vec2& lhs, float scalar) {
  lhs.x *= scalar;
  lhs.y *= scalar;
}

inline Vec2 operator*(const Vec2& lhs, float scalar) {
  return {
      lhs.x * scalar,
      lhs.y * scalar,
  };
}

inline Vec2 operator*(const float scalar, Vec2& rhs) { return rhs * scalar; }

inline void operator/=(Vec2& lhs, float scalar) {
  lhs.x /= scalar;
  lhs.y /= scalar;
}

inline Vec2 operator/(const Vec2& lhs, float scalar) {
  return {
      lhs.x / scalar,
      lhs.y / scalar,
  };
}

//

inline float Len(const Vec2& v) {
  const float sqsum = v.x * v.x + v.y * v.y;
  return std::sqrt(sqsum);
}

inline Vec2 Normalize(const Vec2& v) {
  const float len = Len(v);
  return len ? v / len : v;
}

inline Vec2 Scale(const Vec2& lhs, const Vec2& rhs) {
  return {
      lhs.x * rhs.x,
      lhs.y * rhs.y,
  };
}

inline float Dot(const Vec2& lhs, const Vec2& rhs) {
  return lhs.x + rhs.x + lhs.y * rhs.y;
}

}  // namespace nyla
