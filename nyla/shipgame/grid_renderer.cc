#include "nyla/shipgame/grid_renderer.h"

#include <array>

#include "absl/log/log.h"
#include "nyla/commons/readfile.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

static VkPipeline pipeline;
static VkPipelineLayout pipeline_layout;

void InitGridRenderer() {
  LOG(INFO) << "InitGridRenderer";

  const auto shader_stages = std::to_array<VkPipelineShaderStageCreateInfo>({
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,
          .module = Vulkan_CreateShaderModule(
              ReadFile("nyla/shipgame/shaders/build/grid_vert.spv")),
          .pName = "main",
      },
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = Vulkan_CreateShaderModule(
              ReadFile("nyla/shipgame/shaders/build/grid_frag.spv")),
          .pName = "main",
      },
  });

  const VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .pVertexBindingDescriptions = nullptr,
      .vertexAttributeDescriptionCount = 0,
      .pVertexAttributeDescriptions = nullptr,
  };

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
  };

  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr,
                         &pipeline_layout);

  pipeline = Vulkan_CreateGraphicsPipeline(vertex_input_create_info,
                                           pipeline_layout, shader_stages);
}

void GridRendererRecord() {
  const VkCommandBuffer command_buffer =
      vk.command_buffers[vk.current_frame_data.iframe];

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdDraw(command_buffer, 3, 1, 0, 0);
}

}  // namespace nyla
