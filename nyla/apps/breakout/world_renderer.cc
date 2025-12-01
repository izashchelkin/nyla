#include "nyla/apps/breakout/world_renderer.h"

#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/rhi/rhi.h"

namespace nyla {

namespace {

struct PushConstData {
  Mat4 view;
  Mat4 proj;
};

struct DynamicUbo {
  Mat4 model;
  Vec3f color;
  float pad;
};

}  // namespace

constexpr float kMetersOnScreen = 64.f;

void WorldSetUp() {
  float world_w;
  float world_h;

  const float base = kMetersOnScreen;

  const float width = static_cast<float>(RhiGetSurfaceWidth());
  const float height = static_cast<float>(RhiGetSurfaceHeight());
  const float aspect = width / height;

  world_h = base;
  world_w = base * aspect;

  const PushConstData push_const = {
      .view = Identity4,
      .proj = Ortho(-world_w * .5f, world_w * .5f, world_h * .5f, -world_h * .5f, 0.f, 1.f),
  };
  RpPushConst(world_pipeline, CharViewPtr(&push_const));
}

void WorldRender(Vec2f pos, Vec3f color, float width, float height, const RpMesh& mesh) {
  DynamicUbo dynamic_data{};
  dynamic_data.color = color;

  dynamic_data.model = Translate(pos);
  dynamic_data.model = Mult(dynamic_data.model, ScaleXY(width, height));

  RpDraw(world_pipeline, mesh, CharViewPtr(&dynamic_data));
}

Rp world_pipeline{
    .debug_name = "World",
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
                    RhiVertexAttributeType::Float4,
                },
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "nyla/apps/breakout/shaders/build/world.vert.spv");
          RpAttachFragShader(rp, "nyla/apps/breakout/shaders/build/world.frag.spv");
        },
};

}  // namespace nyla