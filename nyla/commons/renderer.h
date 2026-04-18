#pragma once

#include <cstdint>

#include "nyla/commons/mat.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace Renderer
{

void API Bootstrap(region_alloc &alloc);

void API Mesh(float3 pos, float3 scale, mesh Mesh, texture Texture);
void API CmdFlush(rhi_cmdlist cmd);

void API SetView(float4x4 m);
void API SetLookAtView(float3 eye, float3 center, float3 up);

void API SetProjection(float4x4 m);
void API SetOrthoProjection(uint32_t width, uint32_t height, float metersOnScreen);
void API SetPerspectiveProjection(uint32_t width, uint32_t height, float fovDegrees, float nearPlane, float farPlane);

} // namespace Renderer

} // namespace nyla