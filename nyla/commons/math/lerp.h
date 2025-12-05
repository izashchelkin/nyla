#pragma once

#include <span>

namespace nyla
{

auto Lerp(float a, float b, float p) -> float;

void Lerp(std::span<float> a, std::span<const float> b, float p);

auto LerpAngle(float a, float b, float p) -> float;

} // namespace nyla
