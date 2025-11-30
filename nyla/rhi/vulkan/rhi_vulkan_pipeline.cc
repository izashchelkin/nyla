#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle_pool.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla {

namespace {

struct VulkanPipelineData {
  VkPipelineLayout layout;
  VkPipeline pipeline;
  RhiBindGroupLayout bind_group_layouts[rhi_max_bind_group_layouts];
  uint32_t bind_group_layout_count;
};

rhi_internal::RhiHandlePool<VkShaderModule, 16> shaders;
rhi_internal::RhiHandlePool<VulkanPipelineData, 16> gfx_pipelines;

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

RhiShader RhiCreateShader(RhiShaderDesc desc) {
  using namespace rhi_vulkan_internal;

  const VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = desc.code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(desc.code.data()),
  };

  VkShaderModule shader_module;
  VK_CHECK(vkCreateShaderModule(vk.dev, &create_info, nullptr, &shader_module));

  return static_cast<RhiShader>(RhiHandleAcquire(shaders, shader_module));
}

void RhiDestroyShader(RhiShader shader) {
  using namespace rhi_vulkan_internal;

  VkShaderModule shader_module = RhiHandleRelease(shaders, shader);
  vkDestroyShaderModule(vk.dev, shader_module, nullptr);
}

RhiGraphicsPipeline RhiCreateGraphicsPipeline(RhiGraphicsPipelineDesc desc) {
  using namespace rhi_vulkan_internal;

  VulkanPipelineData pipeline_data = {};

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
        .format = ConvertVulkanFormat(attribute.format),
        .offset = attribute.offset,
    };
  }

  const VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = desc.bind_group_layouts_count,
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

  if (RhiHandleIsSet(desc.vert_shader)) {
    stages[stage_count++] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = RhiHandleGetData(shaders, desc.vert_shader),
        .pName = "main",
    };
  }
  if (RhiHandleIsSet(desc.frag_shader)) {
    stages[stage_count++] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = RhiHandleGetData(shaders, desc.frag_shader),
        .pName = "main",
    };
  }

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
    descriptor_set_layouts[i] = RhiHandleGetData(bind_group_layouts, desc.bind_group_layouts[i]);
  }

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = desc.bind_group_layouts_count,
      .pSetLayouts = descriptor_set_layouts,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant_range,
  };

  VkPipelineLayout layout;
  vkCreatePipelineLayout(vk.dev, &pipeline_layout_create_info, nullptr, &layout);

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
      .layout = layout,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = -1,
  };

  VkPipeline pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

  const RhiGraphicsPipeline ret_handle =
      static_cast<RhiGraphicsPipeline>(RhiHandleAcquire(gfx_pipelines, pipeline_data));
  return ret_handle;
}

void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline pipeline) {
  using namespace rhi_vulkan_internal;

  auto pipeline_data = RhiHandleRelease(gfx_pipelines, pipeline);
  vkDeviceWaitIdle(vk.dev);

  if (pipeline_data.layout) {
    vkDestroyPipelineLayout(vk.dev, pipeline_data.layout, nullptr);
  }
  if (pipeline_data.pipeline) {
    vkDestroyPipeline(vk.dev, pipeline_data.pipeline, nullptr);
  }
}

}  // namespace nyla