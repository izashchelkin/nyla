#include "nyla/shipgame/game_renderer2.h"

#include <unistd.h>

#include "nyla/commons/readfile.h"
#include "nyla/shipgame/simple_graphics_pipeline.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

static void InternalInit(SimplePipeline& pipeline) {
  pipeline.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile("nyla/shipgame/shaders/build/vert.spv")),
      .pName = "main",
  });

  pipeline.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile("nyla/shipgame/shaders/build/frag.spv")),
      .pName = "main",
  });
}

SimplePipeline gamerenderer2_pipeline{
    .uniform =
        {
            .enabled = true,
            .size = 1 << 8,
            .range = 1 << 8,
        },
    .dynamic_uniform =
        {
            .enabled = true,
            .size = 1 << 15,
            .range = 1 << 8,
        },
    .vertex_buffer =
        {
            .enabled = true,
            .size = 1 << 22,
            .slots =
                {
                    SimplePipelineVertexAttributeSlot::Float4,
                    SimplePipelineVertexAttributeSlot::Float4,
                },
        },
    .Init = InternalInit,
};

}  // namespace nyla