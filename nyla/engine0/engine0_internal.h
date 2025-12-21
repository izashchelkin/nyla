#pragma once

#include "nyla/rhi/rhi_shader.h"
#include <string_view>

namespace nyla::engine0_internal
{

struct Engine0Handles
{
};
extern Engine0Handles e0Handles;

auto GetShader(std::string_view name, RhiShaderStage stage) -> RhiShader;

} // namespace nyla::engine0_internal