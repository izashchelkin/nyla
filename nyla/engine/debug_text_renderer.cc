#include "nyla/engine/debug_text_renderer.h"
#include "nyla/commons/byteview.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine0_internal.h"

#include <cstdint>

#include "nyla/rhi/rhi.h"

namespace nyla
{

namespace
{

RhiGraphicsPipeline m_Pipeline;

struct DrawData
{
    std::array<uint4, 17> words;
    int32_t originX;
    int32_t originY;
    uint32_t wordCount;
    uint32_t pad;
    std::array<float, 4> fg;
    std::array<float, 4> bg;
};
InlineVec<DrawData, 4> m_PendingDraws;

} // namespace

using namespace engine0_internal;

void DebugTextRenderer::Init()
{
    const RhiShader vs = GetShader("fullscreen.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("debug_text_renderer.ps", RhiShaderStage::Pixel);

    auto *renderer = new DebugTextRenderer{};

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "DebugTextRender",
        .vs = vs,
        .ps = ps,
        .colorTargetFormats = AssetManager::GetMeshPipelineColorTargetFormats(),
        .depthFormat = RhiTextureFormat::D32_Float_S8_UINT,
    };
    m_Pipeline = Rhi::CreateGraphicsPipeline(pipelineDesc);
}

void DebugTextRenderer::Text(int32_t x, int32_t y, std::string_view text)
{
    DrawData drawData{
        .originX = x,
        .originY = y,
        .wordCount = std::min<uint32_t>((text.size() + 3) / 4, 68),
        .fg = {1.f, 1.f, 1.f, 1.f},
    };

    const size_t numBytes = std::min(text.size(), size_t(drawData.wordCount) * 4);

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

    m_PendingDraws.emplace_back(drawData);
}

void DebugTextRenderer::CmdFlush(RhiCmdList cmd)
{
    if (m_PendingDraws.empty())
        return;

    const uint32_t frameIndex = Rhi::GetFrameIndex();

    Rhi::CmdBindGraphicsPipeline(cmd, m_Pipeline);

    for (const DrawData &drawData : m_PendingDraws)
    {
        Rhi::SetLargeDrawConstant(cmd, ByteViewPtr(&drawData));
        Rhi::CmdDraw(cmd, 3, 1, 0, 0);
    }
    m_PendingDraws.clear();
}

} // namespace nyla