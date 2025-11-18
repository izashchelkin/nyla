#include "nyla/fwk/gui.h"

#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

namespace {

struct Ubo {
  float a;
  float b;
  float c;
  float d;
};

};  // namespace

void UI_BoxBegin();
void UI_Text(std::string_view text);

Rp gui_pipeline{
    .name = "GUI",
    .dynamic_uniform =
        {
            .enabled = true,
            .size = 1 << 20,
            .range = sizeof(Ubo),
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "nyla/fwk/shaders/build/gui.vert.spv");
          RpAttachFragShader(rp, "nyla/fwk/shaders/build/gui.frag.spv");
        },
};

}  // namespace nyla