#pragma once

#include <cstdint>

#include "nyla/platform/abstract_input.h"

namespace nyla
{

#define NYLA_INPUT_MAPPING(X) X(Right) X(Left) X(Boost) X(Fire)

#define X(key) extern const AbstractInputMapping k##key;
NYLA_INPUT_MAPPING(X)
#undef X

void BreakoutInit();
void BreakoutFrame(float dt, uint32_t fps);

} // namespace nyla