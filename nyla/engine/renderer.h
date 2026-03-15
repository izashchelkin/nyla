#pragma once

#include "nyla/commons/mat.h"
#include "nyla/commons/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

class Renderer
{
  public:
    static void Init();

    static void Mesh(float3 pos, float3 scale, AssetManager::Mesh mesh, AssetManager::Texture texture);
    static void CmdFlush(RhiCmdList cmd);

    static void SetView(float4x4 m);
    static void SetLookAtView(float3 eye, float3 center, float3 up);

    static void SetProjection(float4x4 m);
    static void SetOrthoProjection(uint32_t width, uint32_t height, float metersOnScreen);
    static void SetPerspectiveProjection(uint32_t width, uint32_t height, float fovDegrees, float nearPlane,
                                         float farPlane);

  private:
};

} // namespace nyla