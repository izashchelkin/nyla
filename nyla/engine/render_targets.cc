#include "nyla/engine/render_targets.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

void RenderTargets::GetTargets(uint32_t width, uint32_t height, RhiRenderTargetView &outRtv,
                               RhiDepthStencilView &outDsv)
{
    uint32_t frameIndex = g_Rhi.GetFrameIndex();

    if (frameIndex >= m_Rtvs.size())
    {
        RhiTexture texture = g_Rhi.CreateTexture(RhiTextureDesc{
            .width = width,
            .height = height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::ColorTarget | /* RhiTextureUsage::ShaderSampled | */ RhiTextureUsage::TransferSrc,
            .format = m_ColorFormat,
        });
        m_ColorTextures.emplace_back(texture);

        m_Rtvs.emplace_back(g_Rhi.CreateRenderTargetView(RhiRenderTargetViewDesc{
            .texture = texture,
            .format = m_ColorFormat,
        }));
    }

    if (frameIndex >= m_Dsvs.size())
    {
        RhiTexture texture = g_Rhi.CreateTexture(RhiTextureDesc{
            .width = width,
            .height = height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::DepthStencil,
            .format = m_DepthStencilFormat,
        });
        m_DepthStencilTextures.emplace_back(texture);

        m_Dsvs.emplace_back(g_Rhi.CreateDepthStencilView(RhiDepthStencilViewDesc{
            .texture = texture,
            .format = m_DepthStencilFormat,
        }));
    }

    outRtv = m_Rtvs[frameIndex];
    outDsv = m_Dsvs[frameIndex];
}

} // namespace nyla