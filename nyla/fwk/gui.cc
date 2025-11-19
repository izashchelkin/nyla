#include "nyla/fwk/gui.h"

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

void UI_BoxBegin(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  const float xf = x;
  const float yf = y;
  const float zf = 0.f;
  const float wf = 0.f;

#if 1
  const Vec4f rect[] = {
      {xf, yf, zf, wf},
      {xf + width, yf + height, zf, wf},
      {xf + width, yf, zf, wf},
      //
      {xf, yf, zf, wf},
      {xf, yf + height, zf, wf},
      {xf + width, yf + height, zf, wf},
  };
#else
  const Vec4f rect[] = {
      {xf, yf + height, zf, wf},
      {xf + width, yf, zf, wf},
      {xf, yf, zf, wf},
      //
      {xf + width, yf + height, zf, wf},
      {xf + width, yf, zf, wf},
      {xf, yf, zf, wf},
  };
#endif

  RpMesh mesh = RpVertCopy(gui_pipeline, 6, CharViewSpan(std::span{rect, 6}));
  RpDraw(gui_pipeline, mesh, {});

  //
}

void UI_Text(std::string_view text) {
  //
}

Rp gui_pipeline{
    .name = "GUI",
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