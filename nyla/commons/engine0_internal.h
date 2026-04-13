#pragma once

#include "nyla/commons/rhi.h"

namespace nyla::engine0_internal
{

auto GetShader(const char *name, rhi_shader_stage stage) -> rhi_shader;

} // namespace nyla::engine0_internal