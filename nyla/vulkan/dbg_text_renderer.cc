#include "nyla/vulkan/dbg_text_renderer.h"

#include <cstdint>

#include "nyla/commons/memory/charview.h"
#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

namespace {

struct DbgTextLine {
  uint32_t words[68];
  int origin_x;
  int origin_y;
  uint32_t word_count;
  int pad;
  float fg[4];
  float bg[4];
};

}  // namespace

void DbgText(int32_t x, int32_t y, std::string_view text) {
  DbgTextLine ubo{
      .origin_x = x,
      .origin_y = y,
      .word_count = std::min<uint32_t>((text.size() + 3) / 4, 68),
      .fg = {1.f, 1.f, 1.f, 1.f},
  };

  size_t nbytes = std::min(text.size(), size_t(ubo.word_count) * 4);

  for (size_t i = 0; i < ubo.word_count; ++i) {
    uint32_t w = 0;
    for (uint8_t j = 0; j < 4; ++j) {
      size_t idx = i * 4 + j;
      if (idx < nbytes) {
        w |= (uint32_t(uint8_t(text[idx])) << (8 * j));
      }
    }
    ubo.words[i] = w;
  }

  RpDraw(dbg_text_pipeline, {.vert_count = 3}, CharViewPtr(&ubo));
}

Rp dbg_text_pipeline{
    .name = "DbgText",
    .disable_culling = true,
    .dynamic_uniform =
        {
            .enabled = true,
            .size = 1 << 15,
            .range = sizeof(DbgTextLine),
        },
    .Init =
        [](Rp& rp) {
          RpAttachVertShader(rp, "/home/izashchelkin/nyla/nyla/vulkan/shaders/build/dbgtext.vert.spv");
          RpAttachFragShader(rp, "/home/izashchelkin/nyla/nyla/vulkan/shaders/build/dbgtext.frag.spv");
        },
};

}  // namespace nyla