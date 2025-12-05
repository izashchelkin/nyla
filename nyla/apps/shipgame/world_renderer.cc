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
    Mat4 inv_vp;
};

struct DynamicUbo
{
    Mat4 model;
};

} // namespace

constexpr float kMetersOnScreen = 64.f;

void WorldSetUp(Vec2f camera_pos, float zoom)
{
    float world_w;
    float world_h;

    const float base = kMetersOnScreen * zoom;

    const float width = static_cast<float>(RhiGetSurfaceWidth());
    const float height = static_cast<float>(RhiGetSurfaceHeight());
    const float aspect = width / height;
    if (aspect >= 1.0f)
    {
        world_h = base;
        world_w = base * aspect;
    }
    else
    {
        world_w = base;
        world_h = base / aspect;
    }

    Mat4 view = Translate(Vec2fNeg(camera_pos));
    Mat4 proj = Ortho(-world_w * .5f, world_w * .5f, world_h * .5f, -world_h * .5f, 0.f, 1.f);

    Mat4 vp = Mult(proj, view);
    Mat4 inv_vp = Inverse(vp);
    SceneTransforms scene = {vp, inv_vp};

    RpStaticUniformCopy(world_pipeline, CharViewPtr(&scene));
    RpStaticUniformCopy(grid_pipeline, CharViewPtr(&scene));
}

void WorldRender(Vec2f pos, float angle_radians, float scalar, std::span<Vertex> vertices)
{
    Mat4 model = Translate(pos);
    model = Mult(model, Rotate2D(angle_radians));
    model = Mult(model, ScaleXY(scalar, scalar));

    CharView vertex_data = CharViewSpan(vertices);
    CharView dynamic_uniform_data = CharViewPtr(&model);
    RpMesh mesh = RpVertCopy(world_pipeline, vertices.size(), vertex_data);
    RpDraw(world_pipeline, mesh, dynamic_uniform_data);
}

Rp world_pipeline{
    .debug_name = "World",
    .static_uniform =
        {
            .enabled = true,
            .size = sizeof(SceneTransforms),
            .range = sizeof(SceneTransforms),
        },
    .dynamic_uniform =
        {
            .enabled = true,
            .size = 60000,
            .range = sizeof(DynamicUbo),
        },
    .vert_buf =
        {
            .enabled = true,
            .size = 1 << 22,
            .attrs =
                {
                    RhiVertexFormat::R32G32B32A32_Float,
                    RhiVertexFormat::R32G32B32A32_Float,
                },
        },
    .Init =
        [](Rp &rp) -> void {
            RpAttachVertShader(rp, "nyla/apps/shipgame/shaders/build/world.vs.hlsl.spv");
            RpAttachFragShader(rp, "nyla/apps/shipgame/shaders/build/world.ps.hlsl.spv");
        },
};

void GridRender()
{
    RpDraw(grid_pipeline, {.vert_count = 3}, {});
}

Rp grid_pipeline{
    .debug_name = "WorldGrid",
    .static_uniform =
        {
            .enabled = true,
            .size = sizeof(SceneTransforms),
            .range = sizeof(SceneTransforms),
        },
    .Init =
        [](Rp &rp) -> void {
            RpAttachVertShader(rp, "nyla/apps/shipgame/shaders/build/grid.vs.hlsl.spv");
            RpAttachFragShader(rp, "nyla/apps/shipgame/shaders/build/grid.ps.hlsl.spv");
        },
};

} // namespace nyla