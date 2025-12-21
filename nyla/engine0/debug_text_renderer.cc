#include "nyla/engine0/debug_text_renderer.h"

#include <cstdint>

#include "nyla/commons/memory/charview.h"
#include "nyla/engine0/render_pipeline.h"

namespace nyla
{

namespace
{

struct DbgTextLine
{
    uint32_t words[68][4];
    int32_t originX;
    int32_t originY;
    uint32_t wordCount;
    uint32_t pad;
    std::array<float, 4> fg;
    std::array<float, 4> bg;
};

} // namespace

struct DebugTextRenderer
{
    RhiGraphicsPipeline pipeline;
    RhiBindGroupLayout bindGroupLayout;
    RhiBuffer vertexBuffer;
    std::array<RhiBindGroup, kRhiMaxNumFramesInFlight> bindGroup;
    std::array<RhiBuffer, kRhiMaxNumFramesInFlight> dynamicUniformBuffer;
    uint32_t dymamicUniformBufferWritten;
};

void DbgText(int32_t x, int32_t y, std::string_view text)
{
    DbgTextLine ubo{
        .originX = x,
        .originY = y,
        .wordCount = std::min<uint32_t>((text.size() + 3) / 4, 68),
        .fg = {1.f, 1.f, 1.f, 1.f},
    };

    size_t nbytes = std::min(text.size(), size_t(ubo.wordCount) * 4);

    for (size_t i = 0; i < ubo.wordCount; ++i)
    {
        uint32_t w = 0;
        for (uint8_t j = 0; j < 4; ++j)
        {
            size_t idx = i * 4 + j;
            if (idx < nbytes)
            {
                w |= (uint32_t(uint8_t(text[idx])) << (8 * j));
            }
        }
        *(ubo.words[i]) = w;
    }

    RpDraw(dbgTextPipeline, {.vertCount = 3}, ByteViewPtr(&ubo));
}

Rp dbgTextPipeline{
    .debugName = "DbgText",
    .disableCulling = true,
    .dynamicUniform =
        {
            .enabled = true,
            .size = 1 << 15,
            .range = sizeof(DbgTextLine),
        },
    .init = [](Rp &rp) -> void {
        RpAttachVertShader(rp, "/home/izashchelkin/nyla/nyla/shaders/build/dbgtext.vs.hlsl.spv");
        RpAttachFragShader(rp, "/home/izashchelkin/nyla/nyla/shaders/build/dbgtext.ps.hlsl.spv");
    },
};

} // namespace nyla