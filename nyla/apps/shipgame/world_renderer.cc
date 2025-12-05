#include "nyla/apps/shipgame/world_renderer.h"

#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"

namespace nyla
{

namespace
{

struct SceneTransforms
{
    Mat4 vp;
    Mat4 invVp;
};

struct DynamicUbo
{
    Mat4 model;
};

} // namespace

constexpr float kMetersOnScreen = 64.f;

void WorldSetUp(Vec2f cameraPos, float zoom)
{
    float worldW;
    float worldH;

    const float base = kMetersOnScreen * zoom;

    const float width = static_cast<float>(RhiGetSurfaceWidth());
    const float height = static_cast<float>(RhiGetSurfaceHeight());
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

    Mat4 view = Translate(Vec2fNeg(cameraPos));
    Mat4 proj = Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);

    Mat4 vp = Mult(proj, view);
    Mat4 invVp = Inverse(vp);
    SceneTransforms scene = {vp, invVp};

    RpStaticUniformCopy(worldPipeline, CharViewPtr(&scene));
    RpStaticUniformCopy(gridPipeline, CharViewPtr(&scene));
}

void WorldRender(Vec2f pos, float angleRadians, float scalar, std::span<Vertex> vertices)
{
    Mat4 model = Translate(pos);
    model = Mult(model, Rotate2D(angleRadians));
    model = Mult(model, ScaleXY(scalar, scalar));

    CharView vertexData = CharViewSpan(vertices);
    CharView dynamicUniformData = CharViewPtr(&model);
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
    .init =
        [](Rp &rp) -> void {
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
    .init =
        [](Rp &rp) -> void {
            RpAttachVertShader(rp, "nyla/apps/shipgame/shaders/build/grid.vs.hlsl.spv");
            RpAttachFragShader(rp, "nyla/apps/shipgame/shaders/build/grid.ps.hlsl.spv");
        },
};

} // namespace nyla