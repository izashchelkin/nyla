#pragma once

#include "nyla/rhi/rhi_shader.h"

namespace nyla::engine0_internal
{

struct Engine0Handles
{
};
extern Engine0Handles e0Handles;

auto GetShader(const char* name, RhiShaderStage stage) -> RhiShader;

} // namespace nyla::engine0_internal