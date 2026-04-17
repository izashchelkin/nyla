#pragma once

#include "nyla/commons/rhi.h"

namespace nyla
{

auto API GetShader(region_alloc &scratch, byteview name, rhi_shader_stage stage) -> rhi_shader;

} // namespace nyla