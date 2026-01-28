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

    void Rect(float2 pos, float2 dimensions, float4 color);
    void Rect(float2 pos, float2 dimensions, AssetManager::Texture texture);

    void CmdFlush(RhiCmdList cmd, uint32_t width, uint32_t height, float metersOnScreen);

  private:
    RhiGraphicsPipeline m_Pipeline;

    struct VSInput
    {
        float4 pos;
        float4 color;
        float2 uv;
    };

    struct Scene // Per Frame
    {
        float4x4 vp;
        float4x4 invVp;
    };

    struct Entity // Per Draw
    {
        float4x4 model;
        float4 color;
        uint32_t textureIndex;
        uint32_t samplerIndex;
    };

    InlineVec<Entity, 256> m_DrawQueue;
};

} // namespace nyla