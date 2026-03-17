#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "nyla/commons/vec.h"

namespace nyla
{

[[nodiscard]]
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

template <typename T, uint32_t N>
[[nodiscard]]
auto Lerp(const Vec<T, N> &a, const Vec<T, N> &b, T t) -> Vec<T, N>
{
    Vec<T, N> ret;
    for (uint32_t i = 0; i < N; ++i)
        ret[i] = Lerp(a[i], b[i], t);
    return ret;
}

[[nodiscard]]
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