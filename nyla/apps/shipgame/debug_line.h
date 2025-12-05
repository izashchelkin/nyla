#pragma once

#include "nyla/apps/shipgame/shipgame.h"

namespace nyla
{

auto TriangulateLine(const Vec2f &A, const Vec2f &B, float thickness) -> std::vector<Vertex>;

} // namespace nyla