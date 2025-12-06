#include "nyla/apps/shipgame/world_renderer.h"

#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/engine0/render_pipeline.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"

namespace nyla
{

namespace
{

struct SceneTransforms
{
    float4x4 vp;
    float4x4 invVp;
};

struct DynamicUbo
{
    float4 model;
};

} // namespace

constexpr float kMetersOnScreen = 64.f;

void WorldSetUp(float2 cameraPos, float zoom)
{
    float worldW;
    float worldH;

    const float base = kMetersOnScreen * zoom;

    const auto width = static_cast<float>(RhiGetSurfaceWidth());
    const auto height = static_cast<float>(RhiGetSurfaceHeight());
    const float aspect = width / height;
    if (aspect >= 1.0f)
    {
        worldH = base;
        worldW = base * aspect;
    }
    else
    {
        worldW = base;
        worldH = base / aspect;
    }

    float4x4 view = float4x4::Translate(-cameraPos);
    float4x4 proj = float4x4::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);

    float4x4 vp = proj.Mult(view);
    float4x4 invVp = vp.Inversed();
    SceneTransforms scene = {
        .vp = vp,
        .invVp = invVp,
    };

    RpStaticUniformCopy(worldPipeline, ByteViewPtr(&scene));
    RpStaticUniformCopy(gridPipeline, ByteViewPtr(&scene));
}

void WorldRender(float2 pos, float angleRadians, float scalar, std::span<Vertex> vertices)
{
    float4x4 model = float4x4::Translate(pos);
    model = model.Mult(float4x4::Rotate(angleRadians));
    model = model.Mult(float4x4::Scale(float4{scalar, scalar, 1.f, 1.f}));

    ByteView vertexData = ByteViewSpan(vertices);
    ByteView dynamicUniformData = ByteViewPtr(&model);
    RpMesh mesh = RpVertCopy(worldPipeline, vertices.size(), vertexData);
    RpDraw(worldPipeline, mesh, dynamicUniformData);
}

Rp worldPipeline{
    .debugName = "World",
    .staticUniform =
        {
            .enabled = true,
            .size = sizeof(SceneTransforms),
            .range = sizeof(SceneTransforms),
        },
    .dynamicUniform =
        {
            .enabled = true,
            .size = 60000,
            .range = sizeof(DynamicUbo),
        },
    .vertBuf =
        {
            .enabled = true,
            .size = 1 << 22,
            .attrs =
                {
                    RhiVertexFormat::R32G32B32A32Float,
                    RhiVertexFormat::R32G32B32A32Float,
                },
        },
    .init = [](Rp &rp) -> void {
        RpAttachVertShader(rp, "nyla/apps/shipgame/shaders/build/world.vs.hlsl.spv");
        RpAttachFragShader(rp, "nyla/apps/shipgame/shaders/build/world.ps.hlsl.spv");
    },
};

void GridRender()
{
    RpDraw(gridPipeline, {.vertCount = 3}, {});
}

Rp gridPipeline{
    .debugName = "WorldGrid",
    .staticUniform =
        {
            .enabled = true,
            .size = sizeof(SceneTransforms),
            .range = sizeof(SceneTransforms),
        },
    .init = [](Rp &rp) -> void {
        RpAttachVertShader(rp, "nyla/apps/shipgame/shaders/build/grid.vs.hlsl.spv");
        RpAttachFragShader(rp, "nyla/apps/shipgame/shaders/build/grid.ps.hlsl.spv");
    },
};

} // namespace nyla