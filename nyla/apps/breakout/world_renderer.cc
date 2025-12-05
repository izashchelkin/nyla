#include "nyla/apps/breakout/world_renderer.h"

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
    Vec3f color;
    float pad;
};

} // namespace

constexpr float kMetersOnScreen = 64.f;

void WorldSetUp()
{
    float worldW;
    float worldH;

    const float base = kMetersOnScreen;

    const float width = static_cast<float>(RhiGetSurfaceWidth());
    const float height = static_cast<float>(RhiGetSurfaceHeight());
    const float aspect = width / height;

    worldH = base;
    worldW = base * aspect;

    Mat4 view = identity4;
    Mat4 proj = Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);

    Mat4 vp = Mult(proj, view);
    Mat4 invVp = Inverse(vp);
    SceneTransforms scene = {vp, invVp};

    RpPushConst(worldPipeline, CharViewPtr(&scene));
}

void WorldRender(Vec2f pos, Vec3f color, float width, float height, const RpMesh &mesh)
{
    DynamicUbo dynamicData{};
    dynamicData.color = color;

    dynamicData.model = Translate(pos);
    dynamicData.model = Mult(dynamicData.model, ScaleXY(width, height));

    RpDraw(worldPipeline, mesh, CharViewPtr(&dynamicData));
}

Rp worldPipeline{
    .debugName = "World",
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
                },
        },
    .init =
        [](Rp &rp) -> void {
            RpAttachVertShader(rp, "nyla/apps/breakout/shaders/build/world.vs.hlsl.spv");
            RpAttachFragShader(rp, "nyla/apps/breakout/shaders/build/world.ps.hlsl.spv");
        },
};

} // namespace nyla