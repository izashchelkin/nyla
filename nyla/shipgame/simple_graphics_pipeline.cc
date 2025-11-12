#include "nyla/shipgame/simple_graphics_pipeline.h"

#include <unistd.h>

#include <cstdint>

#include "nyla/vulkan/vulkan.h"

namespace nyla {

void SimplePipelineSetUniformData(SimplePipeline& pipeline, void* data) {
  memcpy(pipeline.uniform.mem_mapped[vk.current_frame_data.iframe], data,
         pipeline.uniform.size);
}

void SimplePipelineAdd(SimplePipeline& pipeline, VkBuffer vertex_buffer,
                       void* dynamic_ubo_data) {
  //
}

void SimplePipelineInit(SimplePipeline& pipeline) {
  pipeline.Init(pipeline);

  for (size_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
    if (pipeline.has_uniform) {
      auto& uniform = pipeline.uniform;

      uniform.buffer.resize(kVulkan_NumFramesInFlight);
      uniform.mem.resize(kVulkan_NumFramesInFlight);
      uniform.mem_mapped.resize(kVulkan_NumFramesInFlight);

      CreateUniformBuffer(pipeline.descriptor_sets[i], 0, false, uniform.size,
                          uniform.range, uniform.buffer[i], uniform.mem[i],
                          uniform.mem_mapped[i]);
    }

    if (pipeline.has_dynamic_uniform) {
      auto& uniform = pipeline.dynamic_uniform;

      uniform.buffer.resize(kVulkan_NumFramesInFlight);
      uniform.mem.resize(kVulkan_NumFramesInFlight);
      uniform.mem_mapped.resize(kVulkan_NumFramesInFlight);

      CreateUniformBuffer(pipeline.descriptor_sets[i], 1, true, uniform.size,
                          uniform.range, uniform.buffer[i], uniform.mem[i],
                          uniform.mem_mapped[i]);
    }
  }

  std::vector<VkDescriptorPoolSize> desc_pool_sizes;
  std::vector<VkDescriptorSetLayoutBinding> desc_layout_bindings;

  if (pipeline.has_uniform) {
    desc_layout_bindings.emplace_back(VkDescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    });

    uint32_t descriptor_count = kVulkan_NumFramesInFlight;
    desc_pool_sizes.emplace_back(VkDescriptorPoolSize{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = descriptor_count,
    });
  }

  if (pipeline.has_dynamic_uniform) {
    desc_layout_bindings.emplace_back(VkDescriptorSetLayoutBinding{
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    });

    uint32_t descriptor_count = kVulkan_NumFramesInFlight;
    desc_pool_sizes.emplace_back(VkDescriptorPoolSize{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = descriptor_count,
    });
  }

  uint32_t num_uniforms = pipeline.has_uniform + pipeline.has_dynamic_uniform;
  desc_pool_sizes.reserve(num_uniforms);

  const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = static_cast<uint32_t>(kVulkan_NumFramesInFlight),
      .poolSizeCount = static_cast<uint32_t>(desc_pool_sizes.size()),
      .pPoolSizes = desc_pool_sizes.data(),
  };

  VkDescriptorPool descriptor_pool;
  VK_CHECK(vkCreateDescriptorPool(vk.device, &descriptor_pool_create_info,
                                  nullptr, &descriptor_pool));

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(desc_layout_bindings.size()),
      .pBindings = desc_layout_bindings.data(),
  };

  VkDescriptorSetLayout descriptor_set_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(vk.device,
                                       &descriptor_set_layout_create_info,
                                       nullptr, &descriptor_set_layout));

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr,
                         &pipeline.layout);

  const std::vector<VkDescriptorSetLayout> descriptor_sets_layouts(
      kVulkan_NumFramesInFlight, descriptor_set_layout);

  const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount =
          static_cast<uint32_t>(descriptor_sets_layouts.size()),
      .pSetLayouts = descriptor_sets_layouts.data(),
  };

  pipeline.descriptor_sets.resize(kVulkan_NumFramesInFlight);
  VK_CHECK(vkAllocateDescriptorSets(vk.device, &descriptor_set_alloc_info,
                                    pipeline.descriptor_sets.data()));

  VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  if (pipeline.has_vertex_input) {
    const VkVertexInputBindingDescription binding_description{
        .binding = 0,
        .stride = sizeof(SimplePipelineVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    const VkVertexInputAttributeDescription attr_descriptions[2] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(SimplePipelineVertex, color),
        },
    };

    vertex_input_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_create_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_create_info.vertexAttributeDescriptionCount =
        std::size(attr_descriptions);
    vertex_input_create_info.pVertexAttributeDescriptions = attr_descriptions;
  }

  pipeline.pipeline = Vulkan_CreateGraphicsPipeline(
      vertex_input_create_info, pipeline.layout, pipeline.shader_stages);
}

}  // namespace nyla