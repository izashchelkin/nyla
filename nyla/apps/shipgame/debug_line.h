#pragma once

#include "nyla/apps/shipgame/shipgame.h"

namespace nyla
{

auto TriangulateLine(const Vec2f &a, const Vec2f &b, float thickness) -> std::vector<Vertex>;

} // namespace nyla