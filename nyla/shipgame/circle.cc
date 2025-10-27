#include "nyla/shipgame/circle.h"

#include <cmath>
#include <complex>

#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec2.h"

namespace nyla {

std::vector<Vec2> TriangulateCircle(size_t n, float radius) {
  using namespace std::complex_literals;

  if (n < 4) n = 4;

  std::vector<Vec2> ret;
  ret.reserve(n * 3);

  const float theta = 2.f * pi / n;
  std::complex<float> r = 1.f;

  for (size_t i = 0; i < n; ++i) {
    ret.emplace_back(Vec2{0.f, 0.f});
    ret.emplace_back(Vec2{r.real(), r.imag()} * radius);

    r *= (std::cos(theta) + std::sin(theta) * 1if);

    ret.emplace_back(Vec2{r.real(), r.imag()} * radius);
  }

  return ret;
}

}  // namespace nyla
