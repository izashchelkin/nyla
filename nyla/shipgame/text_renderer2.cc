#include "nyla/shipgame/text_renderer2.h"

#include <unistd.h>

#include "nyla/commons/memory/charview.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/shipgame/simple_graphics_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

namespace {

struct TextRendererLineUBO {
  uint32_t words[68];
  int origin_x;
  int origin_y;
  uint32_t word_count;
  int pad;
  float fg[4];
  float bg[4];
};

}  // namespace

static void InternalInit(Sgp& pipeline) {
  pipeline.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile("nyla/shipgame/shaders/build/psf2_ansii_vert.spv")),
      .pName = "main",
  });

  pipeline.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile("nyla/shipgame/shaders/build/psf2_ansii_frag.spv")),
      .pName = "main",
  });
}

Sgp textrender2_pipeline{
    .dynamic_uniform =
        {
            .enabled = true,
            .size = 1 << 15,
            .range = 1 << 9,
        },
    .Init = InternalInit,
};

void TextRenderer2Line(int32_t x, int32_t y, std::string_view text) {
  TextRendererLineUBO ubo{};

  ubo.origin_x = x;
  ubo.origin_y = y;

  ubo.fg[0] = 1.f;
  ubo.fg[1] = 1.f;
  ubo.fg[2] = 1.f;
  ubo.fg[3] = 1.f;

  ubo.word_count = std::min<size_t>((text.size() + 3) / 4, 68);
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

  SgpObject(textrender2_pipeline, {}, 3, CharViewRef(ubo));
}

}  // namespace nyla