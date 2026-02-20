#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla {

class RenderTargets
{
  public:
    void Init(RhiTextureFormat colorFormat, RhiTextureFormat depthStencilFormat)
    {
        m_ColorFormat = colorFormat;
        m_DepthStencilFormat = depthStencilFormat;
    }

    void GetTargets(uint32_t width, uint32_t height, RhiRenderTargetView &outRtv, RhiDepthStencilView &outDsv);

  private:
    RhiTextureFormat m_ColorFormat;
    InlineVec<RhiTexture, kRhiMaxNumFramesInFlight> m_ColorTextures;
    InlineVec<RhiRenderTargetView, kRhiMaxNumFramesInFlight> m_Rtvs;

    RhiTextureFormat m_DepthStencilFormat;
    InlineVec<RhiTexture, kRhiMaxNumFramesInFlight> m_DepthStencilTextures;
    InlineVec<RhiDepthStencilView, kRhiMaxNumFramesInFlight> m_Dsvs;
};

}