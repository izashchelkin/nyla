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

extern RenderPipeline world_pipeline;

constexpr float kMetersOnScreenY = 2000.f;

void WorldSetUp(Vec2f camera_pos, float zoom) {
  const float aspect = static_cast<float>(vk.surface_extent.width) / static_cast<float>(vk.surface_extent.height);

  const float world_h = kMetersOnScreenY * zoom;
  const float world_w = world_h * aspect;

  const StaticUbo static_ubo = {
      .view = Translate(Vec2fNeg(camera_pos)),
      .proj = Ortho(-world_w * .5f, world_w * .5f, world_h * .5f, -world_h * .5f, 0.f, 1.f),
  };

  RpSetStaticUniform(world_pipeline, CharView(&static_ubo));
}

void WorldRender(Vec2f pos, float angle_radians, std::span<WorldRendererVertex> vertices) {
  Mat4 model = Translate(pos);
  model = Mult(model, Rotate2D(angle_radians));
  model = Mult(model, Scale2D(200.f));

  auto vertex_data = CharViewSpan(vertices);
  auto dynamic_uniform_data = CharView(&model);
  RpDraw(world_pipeline, vertices.size(), vertex_data, dynamic_uniform_data);
}

RenderPipeline world_pipeline{
    .uniform =
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
    .vertex_buffer =
        {
            .enabled = true,
            .size = 1 << 22,
            .attrs =
                {
                    RpVertexAttr::Float4,
                    RpVertexAttr::Float4,
                },
        },
    .Init =
        [](RenderPipeline& rp) {
          RpAttachVertShader(rp, "nyla/shipgame/shaders/build/vert.spv");
          RpAttachFragShader(rp, "nyla/shipgame/shaders/build/frag.spv");
        },
};

}  // namespace nyla