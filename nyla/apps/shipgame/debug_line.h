#pragma once

#include "nyla/apps/shipgame/shipgame.h"

namespace nyla
{

auto TriangulateLine(const float2 &a, const float2 &b, float thickness) -> std::vector<Vertex>;

} // namespace nyla