#include "nyla/shipgame/world_renderer.h"

#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

namespace {

struct StaticUbo {
  Mat4 view;
  Mat4 proj;
};

struct DynamicUbo {
  Mat4 model;
};

}  // namespace

constexpr float kMetersOnScreenY = 64.f;

void WorldSetUp(Vec2f camera_pos, float zoom) {
  const float aspect = static_cast<float>(vk.surface_extent.width) / static_cast<float>(vk.surface_extent.height);

  const float world_h = kMetersOnScreenY * zoom;
  const float world_w = world_h * aspect;

  const StaticUbo static_ubo = {
      .view = Translate(Vec2fNeg(camera_pos)),
      .proj = Ortho(-world_w * .5f, world_w * .5f, world_h * .5f, -world_h * .5f, 0.f, 1.f),
  };

  RpBufCopy(world_pipeline.static_uniform, CharViewPtr(&static_ubo));
  RpBufCopy(grid_pipeline.static_uniform, CharViewPtr(&static_ubo));
}

void WorldRender(Vec2f pos, float angle_radians, float scalar, std::span<WorldRendererVertex> vertices) {
  Mat4 model = Translate(pos);
  model = Mult(model, Rotate2D(angle_radians));
  model = Mult(model, Scale2D(scalar));

  CharView vertex_data = CharViewSpan(vertices);
  CharView dynamic_uniform_data = CharViewPtr(&model);
  RpMesh mesh = RpVertCopy(world_pipeline, vertices.size(), vertex_data);
  RpDraw(world_pipeline, mesh, dynamic_uniform_data);
}

Rp world_pipeline{
    .name = "World",
    .static_uniform =
        {
            .enabled = true,
            .size = sizeof(StaticUbo),
            .range = sizeof(StaticUbo),
        },
    .dynamic_uniform =
        {
            .enabled = true,
            .size = 1 << 15,
            .range = sizeof(DynamicUbo),
        },
    .vert_buf =
        {
            .enabled = true,
            .size = 1 << 22,
            .attrs =
                {
                    RpVertAttr::Float4,
                    RpVertAttr::Float4,
                },
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "nyla/shipgame/shaders/build/vert.spv");
          RpAttachFragShader(rp, "nyla/shipgame/shaders/build/frag.spv");
        },
};

void GridRender() {
  RpDraw(grid_pipeline, {.vert_count = 3}, {});
}

Rp grid_pipeline{
    .name = "WorldGrid",
    .static_uniform =
        {
            .enabled = true,
            .size = sizeof(StaticUbo),
            .range = sizeof(StaticUbo),
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "nyla/shipgame/shaders/build/grid_vert.spv");
          RpAttachFragShader(rp, "nyla/shipgame/shaders/build/grid_frag.spv");
        },
};

}  // namespace nyla