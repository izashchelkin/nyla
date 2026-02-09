#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include <cstdint>

namespace nyla
{

class Renderer
{
  public:
    void Init();

    void Mesh(float3 pos, float3 scale, AssetManager::Mesh mesh, AssetManager::Texture texture);

    void CmdFlush(RhiCmdList cmd, uint32_t width, uint32_t height, float metersOnScreen);

  private:
    RhiGraphicsPipeline m_Pipeline;

    struct Scene // Per Frame
    {
        float4x4 vp;
        float4x4 invVp;
    };

    struct Entity
    {
        float4x4 model;
        uint32_t srvTextureIndex;
        uint32_t samplerIndex;
    };

    struct DrawCall
    {
        Entity entity;
        AssetManager::Mesh mesh;
    };

    InlineVec<DrawCall, 256> m_DrawQueue;
};

} // namespace nyla