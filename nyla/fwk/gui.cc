#include "nyla/fwk/gui.h"

#include <cstdint>

#include "nyla/commons/math/vec/vec4f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"

namespace nyla {

namespace {

struct StaticUbo {
  float window_width;
  float window_height;
};

};  // namespace

void UI_FrameBegin() {
  StaticUbo ubo{static_cast<float>(RhiGetSurfaceWidth()), static_cast<float>(RhiGetSurfaceHeight())};
  RpStaticUniformCopy(gui_pipeline, CharViewPtr(&ubo));
}

static void UI_BoxBegin(float x, float y, float width, float height) {
  const float z = 0.f;
  const float w = 0.f;

  if (x < 0.f) {
    x += RhiGetSurfaceWidth() - width;
  }
  if (y < 0.f) {
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
    .debug_name = "GUI",
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
                    RhiVertexFormat::R32G32B32A32_Float,
                },
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "/home/izashchelkin/nyla/nyla/fwk/shaders/build/gui.vert.spv");
          RpAttachFragShader(rp, "/home/izashchelkin/nyla/nyla/fwk/shaders/build/gui.frag.spv");
        },
};

}  // namespace nyla