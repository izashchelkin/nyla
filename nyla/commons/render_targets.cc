#include "nyla/commons/render_targets.h"

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

namespace RenderTargets
{

void API GetTargets(render_targets &self, uint32_t width, uint32_t height, rhi_rtv *outRtv, rhi_dsv *outDsv)
{
    if (width != self.CachedWidth || height != self.CachedHeight)
    {
        for (rhi_rtv rtv : self.Rtvs)
            Rhi::DestroyRenderTargetView(rtv);
        for (rhi_texture tex : self.ColorTextures)
            Rhi::DestroyTexture(tex);
        for (rhi_dsv dsv : self.Dsvs)
            Rhi::DestroyDepthStencilView(dsv);
        for (rhi_texture tex : self.DepthStencilTextures)
            Rhi::DestroyTexture(tex);

        InlineVec::Clear(self.Rtvs);
        InlineVec::Clear(self.ColorTextures);
        InlineVec::Clear(self.Dsvs);
        InlineVec::Clear(self.DepthStencilTextures);

        self.CachedWidth = width;
        self.CachedHeight = height;
    }

    uint32_t frameIndex = Rhi::GetFrameIndex();

    if (outRtv)
    {
        while (self.Rtvs.size <= frameIndex)
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
        *outRtv = self.Rtvs[frameIndex];
    }

    if (outDsv)
    {
        while (self.Dsvs.size <= frameIndex)
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
        *outDsv = self.Dsvs[frameIndex];
    }
}

} // namespace RenderTargets

} // namespace nyla