#pragma once

#include "nyla/commons/rhi.h"

namespace nyla
{

struct render_targets
{
    rhi_texture_format ColorFormat;
    inline_vec<rhi_texture, kRhiMaxNumFramesInFlight> ColorTextures;
    inline_vec<rhi_rtv, kRhiMaxNumFramesInFlight> Rtvs;

    rhi_texture_format DepthStencilFormat;
    inline_vec<rhi_texture, kRhiMaxNumFramesInFlight> DepthStencilTextures;
    inline_vec<rhi_dsv, kRhiMaxNumFramesInFlight> Dsvs;
};

namespace RenderTargets
{

void API GetTargets(render_targets& self, uint32_t width, uint32_t height, rhi_rtv &outRtv, rhi_dsv &outDsv);

};

} // namespace nyla