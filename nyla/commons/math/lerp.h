#pragma once

#include <span>

namespace nyla {

float Lerp(float a, float b, float p);

void Lerp(std::span<float> a, std::span<const float> b, float p);

float LerpAngle(float a, float b, float p);

}  // namespace nyla
