#include "nyla/commons/math/lerp.h"

#include <algorithm>
#include <span>

#include "absl/log/check.h"
#include "nyla/commons/math/math.h"

namespace nyla
{

auto Lerp(float a, float b, float p) -> float
{
    if (a == b)
        return b;
    p = std::clamp(p, 0.f, 1.f);

    if (p >= 1.f - 1e-6)
    {
        return b;
    }

    float ret = b * p + a * (1.f - p);
    return ret;
}

void Lerp(std::span<float> a, std::span<const float> b, float p)
{
    CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = Lerp(a[i], b[i], p);
}

auto LerpAngle(float a, float b, float p) -> float
{
    if (a == b)
        return b;
    p = std::clamp(p, 0.f, 1.f);

    if (p >= 1.f - 1e-6)
    {
        return b;
    }

    float delta = b - a;
    while (delta > kPi)
        delta -= 2 * kPi;
    while (delta < -kPi)
        delta += 2 * kPi;

    if (std::abs(delta) < 10e-3)
        return b;

    float ret = a + delta * p;
    return ret;
}

} // namespace nyla
