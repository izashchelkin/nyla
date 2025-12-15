#pragma once

#include "nyla/commons/handle.h"

#include <string_view>

namespace nyla
{

struct E0Shader : Handle
{
};

auto E0GetShader(std::string_view name) -> E0Shader;

} // namespace nyla