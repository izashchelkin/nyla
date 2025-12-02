#include <unistd.h>

#include <cstdint>

#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace {

VkCullModeFlags ConvertVulkanCullMode(RhiCullMode cull_mode) {
  switch (cull_mode) {
    case RhiCullMode::None:
      return VK_CULL_MODE_NONE;

    case RhiCullMode::Back:
      return VK_CULL_MODE_BACK_BIT;

    case RhiCullMode::Front:
      return VK_CULL_MODE_FRONT_BIT;
  }
  CHECK(false);
  return static_cast<VkCullModeFlags>(0);
}

VkFrontFace ConvertVulkanFrontFace(RhiFrontFace front_face) {
  switch (front_face) {
    case RhiFrontFace::CCW:
      return VK_FRONT_FACE_COUNTER_CLOCKWISE;

    case RhiFrontFace::CW:
      return VK_FRONT_FACE_CLOCKWISE;
  }
  CHECK(false);
  return static_cast<VkFrontFace>(0);
}

VkVertexInputRate ConvertVulkanInputRate(RhiInputRate input_rate) {
  switch (input_rate) {
    case RhiInputRate::PerInstance:
      return VK_VERTEX_INPUT_RATE_INSTANCE;
    case RhiInputRate::PerVertex:
      return VK_VERTEX_INPUT_RATE_VERTEX;
  }
  CHECK(false);
  return static_cast<VkVertexInputRate>(0);
}

}  // namespace

RhiShader RhiCreateShader(const RhiShaderDesc& desc) {
  const VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = desc.code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(desc.code.data()),
  };

  VkShaderModule shader_module;
  VK_CHECK(vkCreateShaderModule(vk.dev, &create_info, nullptr, &shader_module));

  return RhiHandleAcquire(rhi_handles.shaders, shader_module);
}

void RhiDestroyShader(RhiShader shader) {
  VkShaderModule shader_module = RhiHandleRelease(rhi_handles.shaders, shader);
  vkDestroyShaderModule(vk.dev, shader_module, nullptr);
}

RhiGraphicsPipeline RhiCreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) {
  VulkanPipelineData pipeline_data = {
      .bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS,
  };

  VkVertexInputBindingDescription vertex_bindings[std::size(desc.vertex_bindings)];
  CHECK_LE(desc.vertex_bindings_count, std::size(desc.vertex_bindings));
  for (uint32_t i = 0; i < desc.vertex_bindings_count; ++i) {
    const auto& binding = desc.vertex_bindings[i];
    vertex_bindings[i] = VkVertexInputBindingDescription{
        .binding = binding.binding,
        .stride = binding.stride,
        .inputRate = ConvertVulkanInputRate(binding.input_rate),
    };
  }

  VkVertexInputAttributeDescription vertex_attributes[std::size(desc.vertex_attributes)];
  CHECK_LE(desc.vertex_attribute_count, std::size(desc.vertex_attributes));
  for (uint32_t i = 0; i < desc.vertex_attribute_count; ++i) {
    const auto& attribute = desc.vertex_attributes[i];
    vertex_attributes[i] = VkVertexInputAttributeDescription{
        .location = attribute.location,
        .binding = attribute.binding,
        .format = ConvertVulkanVertexFormat(attribute.format),
        .offset = attribute.offset,
    };
  }

  const VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = desc.vertex_bindings_count,
      .pVertexBindingDescriptions = vertex_bindings,
      .vertexAttributeDescriptionCount = desc.vertex_attribute_count,
      .pVertexAttributeDescriptions = vertex_attributes,
  };

  const VkPipelineRasterizationStateCreateInfo rasterizer_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = ConvertVulkanCullMode(desc.cull_mode),
      .frontFace = ConvertVulkanFrontFace(desc.front_face),
      .lineWidth = 1.0f,
  };

  const VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  const VkPipelineViewportStateCreateInfo viewport_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };

  const VkPipelineMultisampleStateCreateInfo multisampling_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
      .minSampleShading = 1.0f,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable = VK_FALSE,
  };

  const VkPipelineColorBlendAttachmentState color_blend_attachment{
      .blendEnable = VK_TRUE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  const VkPipelineColorBlendStateCreateInfo color_blending_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
      .blendConstants = {},
  };

  const auto dynamic_states = std::to_array<VkDynamicState>({
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  });

  const VkPipelineDynamicStateCreateInfo dynamic_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = dynamic_states.size(),
      .pDynamicStates = dynamic_states.data(),
  };

  const VkPipelineRenderingCreateInfo pipeline_rendering_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &vk.surface_format.format,
  };

  VkPipelineShaderStageCreateInfo stages[2];
  uint32_t stage_count = 0;

  stages[stage_count++] = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = RhiHandleGetData(rhi_handles.shaders, desc.vert_shader),
      .pName = "main",
  };

  stages[stage_count++] = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = RhiHandleGetData(rhi_handles.shaders, desc.frag_shader),
      .pName = "main",
  };

  CHECK_LE(desc.bind_group_layouts_count, std::size(desc.bind_group_layouts));
  pipeline_data.bind_group_layout_count = desc.bind_group_layouts_count;
  memcpy(pipeline_data.bind_group_layouts, desc.bind_group_layouts,
         desc.bind_group_layouts_count * sizeof(pipeline_data.bind_group_layouts[0]));

  const VkPushConstantRange push_constant_range{
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = rhi_max_push_constant_size,
  };

  VkDescriptorSetLayout descriptor_set_layouts[rhi_max_bind_group_layouts];
  for (uint32_t i = 0; i < desc.bind_group_layouts_count; ++i) {
    descriptor_set_layouts[i] = RhiHandleGetData(rhi_handles.bind_group_layouts, desc.bind_group_layouts[i]);
  }

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = desc.bind_group_layouts_count,
      .pSetLayouts = descriptor_set_layouts,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant_range,
  };

  vkCreatePipelineLayout(vk.dev, &pipeline_layout_create_info, nullptr, &pipeline_data.layout);

  const VkGraphicsPipelineCreateInfo pipeline_create_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pipeline_rendering_create_info,
      .stageCount = stage_count,
      .pStages = stages,
      .pVertexInputState = &vertex_input_state_create_info,
      .pInputAssemblyState = &input_assembly_create_info,
      .pViewportState = &viewport_state_create_info,
      .pRasterizationState = &rasterizer_create_info,
      .pMultisampleState = &multisampling_create_info,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_blending_create_info,
      .pDynamicState = &dynamic_state_create_info,
      .layout = pipeline_data.layout,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = -1,
  };

  VK_CHECK(
      vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline_data.pipeline));

  return RhiHandleAcquire(rhi_handles.graphics_pipelines, pipeline_data);
}

void RhiNameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name) {
  const VulkanPipelineData& pipeline_data = RhiHandleGetData(rhi_handles.graphics_pipelines, pipeline);
  VulkanNameHandle(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline_data.pipeline, name);
}

void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline pipeline) {
  auto pipeline_data = RhiHandleRelease(rhi_handles.graphics_pipelines, pipeline);
  vkDeviceWaitIdle(vk.dev);

  if (pipeline_data.layout) {
    vkDestroyPipelineLayout(vk.dev, pipeline_data.layout, nullptr);
  }
  if (pipeline_data.pipeline) {
    vkDestroyPipeline(vk.dev, pipeline_data.pipeline, nullptr);
  }
}

void RhiCmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline) {
  VulkanCmdListData& cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);
  const VulkanPipelineData& pipeline_data = RhiHandleGetData(rhi_handles.graphics_pipelines, pipeline);

  vkCmdBindPipeline(cmd_data.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_data.pipeline);
  cmd_data.bound_graphics_pipeline = pipeline;
}

void RhiCmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, CharView data) {
  const VulkanCmdListData& cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);
  const VulkanPipelineData& pipeline_data =
      RhiHandleGetData(rhi_handles.graphics_pipelines, cmd_data.bound_graphics_pipeline);

  vkCmdPushConstants(cmd_data.cmdbuf, pipeline_data.layout, ConvertVulkanStageFlags(stage), offset, data.size(),
                     data.data());
}

void RhiCmdBindVertexBuffers(RhiCmdList cmd, uint32_t first_binding, std::span<const RhiBuffer> buffers,
                             std::span<const uint32_t> offsets) {
  CHECK_EQ(buffers.size(), offsets.size());
  CHECK_LE(buffers.size(), 4U);

  VkBuffer vk_bufs[4]{};
  VkDeviceSize vk_offsets[4];
  for (uint32_t i = 0; i < buffers.size(); ++i) {
    vk_bufs[i] = RhiHandleGetData(rhi_handles.buffers, buffers[i]).buffer;
    vk_offsets[i] = offsets[i];
  }

  VulkanCmdListData cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);
  vkCmdBindVertexBuffers(cmd_data.cmdbuf, first_binding, buffers.size(), vk_bufs, vk_offsets);
}

void RhiCmdDraw(RhiCmdList cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
                uint32_t first_instance) {
  VulkanCmdListData cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);
  vkCmdDraw(cmd_data.cmdbuf, vertex_count, instance_count, first_vertex, first_instance);
}

}  // namespace nyla