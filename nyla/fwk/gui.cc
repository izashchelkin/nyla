#include "nyla/fwk/gui.h"

#include <cstdint>

#include "nyla/commons/math/vec/vec4f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"

namespace nyla
{

namespace
{

struct StaticUbo
{
    float windowWidth;
    float windowHeight;
};

}; // namespace

void UiFrameBegin()
{
    StaticUbo ubo{static_cast<float>(RhiGetSurfaceWidth()), static_cast<float>(RhiGetSurfaceHeight())};
    RpStaticUniformCopy(guiPipeline, CharViewPtr(&ubo));
}

static void UiBoxBegin(float x, float y, float width, float height)
{
    const float z = 0.f;
    const float w = 0.f;

    if (x < 0.f)
    {
        x += RhiGetSurfaceWidth() - width;
    }
    if (y < 0.f)
    {
        y += RhiGetSurfaceHeight() - height;
    }

    const Vec4f rect[] = {
        {x, y, z, w},
        {x + width, y + height, z, w},
        {x + width, y, z, w},
        //
        {x, y, z, w},
        {x, y + height, z, w},
        {x + width, y + height, z, w},
    };

    RpMesh mesh = RpVertCopy(guiPipeline, std::size(rect), CharViewSpan(std::span{rect, std::size(rect)}));
    RpDraw(guiPipeline, mesh, {});
}

void UiBoxBegin(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    UiBoxBegin((float)x, (float)y, (float)width, (float)height);
}

void UiText(std::string_view text)
{
    //
}

Rp guiPipeline{
    .debugName = "GUI",
    .disableCulling = true,
    .staticUniform =
        {
            .enabled = true,
            .size = sizeof(StaticUbo),
            .range = sizeof(StaticUbo),
        },
    .vertBuf =
        {
            .enabled = true,
            .size = 1 << 20,
            .attrs =
                {
                    RhiVertexFormat::R32G32B32A32Float,
                },
        },
    .init =
        [](Rp &rp) -> void {
            RpAttachVertShader(rp, "/home/izashchelkin/nyla/nyla/fwk/shaders/build/gui.vs.hlsl.spv");
            RpAttachFragShader(rp, "/home/izashchelkin/nyla/nyla/fwk/shaders/build/gui.ps.hlsl.spv");
        },
};

} // namespace nyla