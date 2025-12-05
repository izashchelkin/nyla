#pragma once

#include "nyla/commons/math/vec/vec3f.h"

namespace nyla
{

auto ConvertHsvToRgb(float h, float s, float v) -> Vec3f;

} // namespace nyla