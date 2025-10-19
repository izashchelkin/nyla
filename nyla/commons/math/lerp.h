#pragma once

#include <algorithm>
#include <cmath>

inline float Lerp(float v0, float v1, float t) {
  if (v0 == v1) return v1;

  t = std::clamp(t, 0.f, 1.f);
  float ret = v1 * t + v0 * (1.f - t);
  if (std::abs(ret - v1) <= 10e-3)
    return v1;
  else
    return ret;
}

inline float AngleDelta(float from, float to) {
  return std::atan2(std::sin(to - from), std::cos(to - from));
}

inline float RotateTowards(float current, float target, float max_speed,
                           float dt) {
  float d = AngleDelta(current, target);

  constexpr float kPi = std::numbers::pi_v<float>;
  if (std::abs(std::abs(d) - kPi) < 1e-7f) {
    d = std::copysign(kPi, d);
  }

  constexpr float kEpsilon = 1e-5f;
  if (std::abs(d) < kEpsilon) return current + d;

  const float max_step = max_speed * dt;
  const float step = std::clamp(d, -max_step, max_step);
  return current + step;
}
