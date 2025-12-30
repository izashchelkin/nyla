#include "nyla/commons/math/lerp.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

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
    while (delta > std::numbers::pi_v<float>)
        delta -= 2 * std::numbers::pi_v<float>;
    while (delta < -std::numbers::pi_v<float>)
        delta += 2 * std::numbers::pi_v<float>;

    if (std::abs(delta) < 10e-3)
        return b;

    float ret = a + delta * p;
    return ret;
}

} // namespace nyla