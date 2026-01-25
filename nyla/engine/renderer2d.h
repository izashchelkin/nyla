#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include <cstdint>

namespace nyla
{

class Renderer2D
{
  public:
    void Init();

    void FrameBegin(RhiCmdList cmd);
    void Rect(RhiCmdList cmd, float x, float y, float width, float height, float4 color, uint32_t textureIndex);
    void Draw(RhiCmdList cmd, uint32_t width, uint32_t height, float metersOnScreen);

  private:
    RhiGraphicsPipeline m_Pipeline;
    RhiBuffer m_VertexBuffer;

    struct VSInput
    {
        float4 pos;
        float4 color;
        float2 uv;
    };

    struct EntityUbo // Per Draw
    {
        float4x4 model;
        float4 color;
        uint32_t textureIndex;
        uint32_t samplerIndex;
    };

    struct Scene // Per Frame
    {
        float4x4 vp;
        float4x4 invVp;
    };

    InlineVec<EntityUbo, 256> m_PendingDraws;
};

} // namespace nyla