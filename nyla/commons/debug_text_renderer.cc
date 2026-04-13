#include "nyla/commons/debug_text_renderer.h"

#include <cstdint>

#include "nyla/commons/asset_manager.h"
#include "nyla/commons/engine0_internal.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace
{

RhiGraphicsPipeline m_Pipeline;

struct DrawData
{
    array<uint4, 17> words;
    int32_t originX;
    int32_t originY;
    uint32_t wordCount;
    uint32_t pad;
    array<float, 4> fg;
    array<float, 4> bg;
};
inline_vec<DrawData, 4> m_PendingDraws;

} // namespace

void DebugTextRenderer::Init()
{
    const RhiShader vs = GetShader("fullscreen.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("debug_text_renderer.ps", RhiShaderStage::Pixel);

    auto *renderer = new DebugTextRenderer{};

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "DebugTextRender"_s,
        .vs = vs,
        .ps = ps,
        .colorTargetFormats = AssetManager::GetMeshPipelineColorTargetFormats(),
        .depthFormat = RhiTextureFormat::D32_Float_S8_UINT,
    };
    m_Pipeline = Rhi::CreateGraphicsPipeline(pipelineDesc);
}

void DebugTextRenderer::Text(int32_t x, int32_t y, byteview text)
{
    DrawData drawData{
        .originX = x,
        .originY = y,
        .wordCount = std::min<uint32_t>((text.Size() + 3) / 4, 68),
        .fg = {1.f, 1.f, 1.f, 1.f},
    };

    const size_t numBytes = std::min(text.Size(), size_t(drawData.wordCount) * 4);

    for (size_t i = 0; i < drawData.wordCount; ++i)
    {
        uint32_t word = 0;
        for (uint8_t j = 0; j < 4; ++j)
        {
            const uint32_t idx = i * 4 + j;
            if (idx < numBytes)
                word |= (uint32_t(uint8_t(text[idx])) << (8 * j));
        }

        drawData.words[i / 4][i % 4] = word;
    }

    m_PendingDraws.PushBack(drawData);
}

void DebugTextRenderer::CmdFlush(RhiCmdList cmd)
{
    if (m_PendingDraws.Empty())
        return;

    const uint32_t frameIndex = Rhi::GetFrameIndex();

    Rhi::CmdBindGraphicsPipeline(cmd, m_Pipeline);

    for (const DrawData &drawData : m_PendingDraws)
    {
        Rhi::SetLargeDrawConstant(cmd, ByteViewPtr(&drawData));
        Rhi::CmdDraw(cmd, 3, 1, 0, 0);
    }
    m_PendingDraws.Clear();
}

} // namespace nyla