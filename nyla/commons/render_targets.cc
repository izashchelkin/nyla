#include "nyla/commons/render_targets.h"

#include "nyla/commons/rhi.h"

namespace nyla
{

namespace RenderTargets
{

void API GetTargets(render_targets &self, uint32_t width, uint32_t height, rhi_rtv &outRtv, rhi_dsv &outDsv)
{
    uint32_t frameIndex = Rhi::GetFrameIndex();

    if (frameIndex >= self.Rtvs.size)
    {
        rhi_texture Texture = Rhi::CreateTexture(rhi_texture_desc{
            .width = width,
            .height = height,
            .memoryUsage = rhi_memory_usage::GpuOnly,
            .usage = rhi_texture_usage::ColorTarget |
                     /* rhi_texture_usage::ShaderSampled | */ rhi_texture_usage::TransferSrc,
            .format = self.ColorFormat,
        });
        InlineVec::Append(self.ColorTextures, Texture);

        InlineVec::Append(self.Rtvs, Rhi::CreateRenderTargetView(rhi_render_target_view_desc{
                                         .texture = Texture,
                                         .format = self.ColorFormat,
                                     }));
    }
    outRtv = self.Rtvs[frameIndex];

    if (frameIndex >= self.Dsvs.size)
    {
        rhi_texture Texture = Rhi::CreateTexture(rhi_texture_desc{
            .width = width,
            .height = height,
            .memoryUsage = rhi_memory_usage::GpuOnly,
            .usage = rhi_texture_usage::DepthStencil,
            .format = self.DepthStencilFormat,
        });
        InlineVec::Append(self.DepthStencilTextures, Texture);

        InlineVec::Append(self.Dsvs, Rhi::CreateDepthStencilView(rhi_depth_stencil_view_desc{
                                         .texture = Texture,
                                         .format = self.DepthStencilFormat,
                                     }));
    }
    outDsv = self.Dsvs[frameIndex];
}

} // namespace RenderTargets

} // namespace nyla