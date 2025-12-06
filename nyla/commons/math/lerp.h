#pragma once

#include "nyla/commons/math/vec.h"
#include <cstdint>

namespace nyla
{

auto Lerp(float a, float b, float p) -> float;

template <typename T, uint32_t N> void Lerp(Vec<T, N> &a, const Vec<T, N> &b, float p)
{
    for (uint32_t i = 0; i < N; ++i)
        a[i] = Lerp(a[i], b[i], p);
}

auto LerpAngle(float a, float b, float p) -> float;

} // namespace nyla