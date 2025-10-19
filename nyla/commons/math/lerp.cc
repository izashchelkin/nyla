#include <algorithm>

#include "nyla/commons/math/math.h"

namespace nyla {

float Lerp(float a, float b, float p) {
  if (a == b) return b;
  p = std::clamp(p, 0.f, 1.f);

  float ret = b * p + a * (1.f - p);
  return ret;
}

float LerpAngle(float a, float b, float p) {
  if (a == b) return b;
  p = std::clamp(p, 0.f, 1.f);

  float delta = b - a;
  while (delta > pi) delta -= 2 * pi;
  while (delta < -pi) delta += 2 * pi;

  if (std::abs(delta) < 10e-3) return b;

  float ret = a + delta * p;
  return ret;
}

}  // namespace nyla
