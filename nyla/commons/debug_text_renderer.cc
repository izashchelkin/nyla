#include "nyla/commons/debug_text_renderer.h"

#include <cstdint>

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

rhi_graphics_pipeline m_Pipeline;

struct draw_data
{
    array<uint4, 17> words;
    int32_t originX;
    int32_t originY;
    uint32_t wordCount;
    uint32_t pad;
    array<float, 4> fg;
    array<float, 4> bg;
};

struct debug_text_renderer
{
    inline_vec<draw_data, 4> pendingDraws;
};
debug_text_renderer* renderer;

} // namespace

namespace DebugTextRenderer
{

void API Bootstrap(region_alloc &alloc)
{
    renderer = &RegionAlloc::Alloc<debug_text_renderer>(RegionAlloc::g_BootstrapAlloc);

    const rhi_shader vs = GetShader(alloc, "fullscreen.vs"_s, rhi_shader_stage::Vertex);
    const rhi_shader ps = GetShader(alloc, "debug_text_renderer.ps"_s, rhi_shader_stage::Pixel);

    rhi_texture_format colorFormat = rhi_texture_format::R8G8B8A8_sRGB;
    const rhi_graphics_pipeline_desc pipelineDesc{
        .debugName = "DebugTextRender"_s,
        .vs = vs,
        .ps = ps,
        .colorTargetFormats = {&colorFormat, 1},
        .depthFormat = rhi_texture_format::D32_Float_S8_UINT,
    };
    m_Pipeline = Rhi::CreateGraphicsPipeline(alloc, pipelineDesc);
}

void API Text(int32_t x, int32_t y, byteview text)
{
    draw_data drawData{
        .originX = x,
        .originY = y,
        .wordCount = Min<uint32_t>((text.size + 3) / 4, 68),
        .fg = {1.f, 1.f, 1.f, 1.f},
    };

    const uint64_t numBytes = Min(text.size, uint64_t(drawData.wordCount) * 4);

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

    InlineVec::Append(renderer->pendingDraws, drawData);
}

void API CmdFlush(rhi_cmdlist cmd)
{
    if (renderer->pendingDraws.size == 0)
        return;

    const uint32_t frameIndex = Rhi::GetFrameIndex();

    Rhi::CmdBindGraphicsPipeline(cmd, m_Pipeline);

    for (const draw_data &drawData : renderer->pendingDraws)
    {
        Rhi::SetLargeDrawConstant(cmd, Span::ByteViewPtr(&drawData));
        Rhi::CmdDraw(cmd, 3, 1, 0, 0);
    }

    InlineVec::Clear(renderer->pendingDraws);
}

} // namespace DebugTextRenderer

} // namespace nyla