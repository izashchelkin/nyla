#include "nyla/commons/render_targets.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

void RenderTargets::GetTargets(uint32_t width, uint32_t height, RhiRenderTargetView &outRtv,
                               RhiDepthStencilView &outDsv)
{
    uint32_t frameIndex = Rhi::GetFrameIndex();

    if (frameIndex >= m_Rtvs.Size())
    {
        RhiTexture texture = Rhi::CreateTexture(RhiTextureDesc{
            .width = width,
            .height = height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::ColorTarget | /* RhiTextureUsage::ShaderSampled | */ RhiTextureUsage::TransferSrc,
            .format = m_ColorFormat,
        });
        m_ColorTextures.PushBack(texture);

        m_Rtvs.PushBack(Rhi::CreateRenderTargetView(RhiRenderTargetViewDesc{
            .texture = texture,
            .format = m_ColorFormat,
        }));
    }

    if (frameIndex >= m_Dsvs.Size())
    {
        RhiTexture texture = Rhi::CreateTexture(RhiTextureDesc{
            .width = width,
            .height = height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::DepthStencil,
            .format = m_DepthStencilFormat,
        });
        m_DepthStencilTextures.PushBack(texture);

        m_Dsvs.PushBack(Rhi::CreateDepthStencilView(RhiDepthStencilViewDesc{
            .texture = texture,
            .format = m_DepthStencilFormat,
        }));
    }

    outRtv = m_Rtvs[frameIndex];
    outDsv = m_Dsvs[frameIndex];
}

} // namespace nyla