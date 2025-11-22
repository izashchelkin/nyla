#include "nyla/apps/breakout/world_renderer.h"

#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/vulkan/render_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

namespace {

struct PushConstData {
  Mat4 view;
  Mat4 proj;
};

struct DynamicUbo {
  Mat4 model;
};

}  // namespace

constexpr float kMetersOnScreen = 64.f;

void WorldSetUp() {
  float world_w;
  float world_h;

  const float base = kMetersOnScreen;

  const float width = static_cast<float>(vk.surface_extent.width);
  const float height = static_cast<float>(vk.surface_extent.height);
  const float aspect = width / height;

  world_h = base;
  world_w = base * aspect;

  const PushConstData push_const = {
      .view = Identity4,
      .proj = Ortho(-world_w * .5f, world_w * .5f, world_h * .5f, -world_h * .5f, 0.f, 1.f),
  };
  RpPushConst(world_pipeline, CharViewPtr(&push_const));
}

void WorldRender(Vec2f pos, std::span<Vertex> vertices) {
  Mat4 model = Translate(pos);

  CharView vertex_data = CharViewSpan(vertices);
  CharView dynamic_uniform_data = CharViewPtr(&model);
  RpMesh mesh = RpVertCopy(world_pipeline, vertices.size(), vertex_data);
  RpDraw(world_pipeline, mesh, dynamic_uniform_data);
}

Rp world_pipeline{
    .name = "World",
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
          RpAttachVertShader(rp, "nyla/apps/breakout/shaders/build/world.vert.spv");
          RpAttachFragShader(rp, "nyla/apps/breakout/shaders/build/world.frag.spv");
        },
};

}  // namespace nyla