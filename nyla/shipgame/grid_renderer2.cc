#include <unistd.h>

#include "nyla/commons/readfile.h"
#include "nyla/shipgame/game_renderer2.h"
#include "nyla/shipgame/simple_graphics_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

static void InternalInit(SimplePipeline& pipeline) {
  pipeline.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile("nyla/shipgame/shaders/build/grid_vert.spv")),
      .pName = "main",
  });

  pipeline.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile("nyla/shipgame/shaders/build/grid_frag.spv")),
      .pName = "main",
  });
}

SimplePipeline gridrenderer2_pipeline{
    .Init = InternalInit,
};

void GridRenderer2Render() { SimplePipelineObject(gridrenderer2_pipeline, {}, 3, {}); }

}  // namespace nyla