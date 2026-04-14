#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/math.h"
#include "nyla/commons/minmax.h"

namespace nyla
{

[[nodiscard]]
INLINE auto Lerp(float a, float b, float p) -> float
{
    if (a == b)
        return b;
    p = Clamp(p, 0.f, 1.f);

    if (p >= 1.f - 1e-6)
    {
        return b;
    }

    float ret = b * p + a * (1.f - p);
    return ret;
}

template <typename T, uint64_t N>
[[nodiscard]]
INLINE auto Lerp(const array<T, N> &a, const array<T, N> &b, T t) -> array<T, N>
{
    array<T, N> ret;
    for (uint32_t i = 0; i < N; ++i)
        ret[i] = Lerp(a[i], b[i], t);
    return ret;
}

[[nodiscard]]
INLINE auto LerpAngle(float a, float b, float p) -> float
{
    if (a == b)
        return b;
    p = Clamp(p, 0.f, 1.f);

    if (p >= 1.f - 1e-6)
    {
        return b;
    }

    float delta = b - a;
    while (delta > math::pi)
        delta -= 2 * math::pi;
    while (delta < -math::pi)
        delta += 2 * math::pi;

    if (std::abs(delta) < 10e-3)
        return b;

    float ret = a + delta * p;
    return ret;
}

} // namespace nyla