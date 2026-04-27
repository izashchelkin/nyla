#pragma once

#include <cstdint>

#include "nyla/commons/rhi.h"

namespace nyla
{

auto API GetShader(uint64_t guid, rhi_shader_stage stage) -> rhi_shader;

} // namespace nyla
