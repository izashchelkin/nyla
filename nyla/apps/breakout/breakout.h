#pragma once

#include <cstdint>
#include <vector>

#include "nyla/commons/containers/map.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/fwk/input.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

#define NYLA_INPUT_MAPPING(X) X(Right) X(Left) X(Boost) X(Fire)

#define X(key) extern const InputMappingId k##key;
NYLA_INPUT_MAPPING(X)
#undef X

void BreakoutInit();
void BreakoutProcess();
void BreakoutRender();

}  // namespace nyla