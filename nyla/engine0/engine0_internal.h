#pragma once

#include "nyla/commons/handle_pool.h"
#include "nyla/engine0/shader_manager.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/spirview/spirview.h"

namespace nyla::engine0_internal
{

struct E0ShaderData
{
    std::string_view name;
    RhiShader shader;
    SpirviewReflectResult reflectResult;
};

struct Engine0Handles
{
    HandlePool<E0Shader, E0ShaderData, 16> shaders;
};
extern Engine0Handles e0Handles;

} // namespace nyla::engine0_internal