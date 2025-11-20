#include "nyla/fwk/gui.h"

#include <cstdint>

#include "nyla/commons/math/vec/vec4f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

namespace {

struct StaticUbo {
  float window_width;
  float window_height;
};

};  // namespace

void UI_FrameBegin() {
  StaticUbo ubo{static_cast<float>(vk.surface_extent.width), static_cast<float>(vk.surface_extent.height)};
  RpStaticUniformCopy(gui_pipeline, CharViewPtr(&ubo));
}

static void UI_BoxBegin(float x, float y, float width, float height) {
  const float z = 0.f;
  const float w = 0.f;

  if (x < 0.f) {
    x += vk.surface_extent.width - width;
  }
  if (y < 0.f) {
    y += vk.surface_extent.height - height;
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

  RpMesh mesh = RpVertCopy(gui_pipeline, std::size(rect), CharViewSpan(std::span{rect, std::size(rect)}));
  RpDraw(gui_pipeline, mesh, {});
}

void UI_BoxBegin(int32_t x, int32_t y, uint32_t width, uint32_t height) {
  UI_BoxBegin((float)x, (float)y, (float)width, (float)height);
}

void UI_Text(std::string_view text) {
  //
}

Rp gui_pipeline{
    .name = "GUI",
    .disable_culling = true,
    .static_uniform =
        {
            .enabled = true,
            .size = sizeof(StaticUbo),
            .range = sizeof(StaticUbo),
        },
    .vert_buf =
        {
            .enabled = true,
            .size = 1 << 20,
            .attrs =
                {
                    RpVertAttr::Float4,
                },
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "nyla/fwk/shaders/build/gui.vert.spv");
          RpAttachFragShader(rp, "nyla/fwk/shaders/build/gui.frag.spv");
        },
};

}  // namespace nyla