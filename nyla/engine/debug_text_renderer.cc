#include "nyla/engine/debug_text_renderer.h"
#include "nyla/commons/byteview.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine0_internal.h"

#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

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
    };
    m_Pipeline = g_Rhi.CreateGraphicsPipeline(pipelineDesc);
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

    const uint32_t frameIndex = g_Rhi.GetFrameIndex();

    g_Rhi.CmdBindGraphicsPipeline(cmd, m_Pipeline);

    for (const DrawData &drawData : m_PendingDraws)
    {
        g_Rhi.SetLargeDrawConstant(cmd, ByteViewPtr(&drawData));
        g_Rhi.CmdDraw(cmd, 3, 1, 0, 0);
    }
    m_PendingDraws.clear();
}

} // namespace nyla