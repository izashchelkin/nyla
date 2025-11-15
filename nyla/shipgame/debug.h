#pragma once

#include "nyla/shipgame/game.h"

namespace nyla {

std::vector<Vertex> TriangulateLine(const Vec2f& A, const Vec2f& B, float thickness);

}  // namespace nyla