#pragma once

#include "nyla/commons/rhi.h"

namespace nyla::engine0_internal
{

auto GetShader(const char *name, RhiShaderStage stage) -> RhiShader;

} // namespace nyla::engine0_internal