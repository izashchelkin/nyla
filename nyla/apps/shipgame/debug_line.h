#pragma once

#include "nyla/apps/shipgame/shipgame.h"

namespace nyla {

std::vector<WorldRendererVertex> TriangulateLine(const Vec2f& A, const Vec2f& B, float thickness);

}  // namespace nyla