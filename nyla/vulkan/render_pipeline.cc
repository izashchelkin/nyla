#include "nyla/vulkan/render_pipeline.h"

#include <unistd.h>

#include <cstdint>

#include "nyla/commons/memory/align.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

static void CreateMappedBuffer(VkBufferUsageFlags usage, size_t buffer_size, VkBuffer& buffer, VkDeviceMemory& memory,
                               void*& mapped) {
  Vulkan_CreateBuffer(buffer_size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buffer, memory);

  vkMapMemory(vk.device, memory, 0, buffer_size, 0, &mapped);
}

static void CreateMappedUniformBuffer(VkDescriptorSet descriptor_set, uint32_t dst_binding, bool dynamic,
                                      uint32_t buffer_size, uint32_t range, VkBuffer& buffer, VkDeviceMemory& memory,
                                      void*& mapped) {
  CreateMappedBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer_size, buffer, memory, mapped);

  VkDescriptorBufferInfo descriptor_buffer_info{
      .buffer = buffer,
      .offset = 0,
      .range = range,
  };

  VkWriteDescriptorSet write_descriptor_set{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .dstBinding = dst_binding,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .pImageInfo = nullptr,
      .pBufferInfo = &descriptor_buffer_info,
      .pTexelBufferView = nullptr,
  };

  vkUpdateDescriptorSets(vk.device, 1, &write_descriptor_set, 0, nullptr);
}

static uint32_t CalcVertexBufferStride(RenderPipeline& pipeline) {
  uint32_t ret = 0;
  for (auto attr : pipeline.vertex_buffer.attrs) {
    ret += RpVertexAttrSize(attr);
  }
  return ret;
}

void RpInit(RenderPipeline& pipeline) {
  pipeline.shader_stages.clear();

  pipeline.Init(pipeline);

  std::vector<VkDescriptorPoolSize> desc_pool_sizes;
  std::vector<VkDescriptorSetLayoutBinding> desc_layout_bindings;

  const uint32_t num_uniforms = pipeline.uniform.enabled + pipeline.dynamic_uniform.enabled;
  desc_pool_sizes.reserve(num_uniforms);

  if (pipeline.uniform.enabled) {
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

  if (pipeline.dynamic_uniform.enabled) {
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

  const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = static_cast<uint32_t>(kVulkan_NumFramesInFlight),
      .poolSizeCount = static_cast<uint32_t>(desc_pool_sizes.size()),
      .pPoolSizes = desc_pool_sizes.data(),
  };

  VkDescriptorPool descriptor_pool;
  VK_CHECK(vkCreateDescriptorPool(vk.device, &descriptor_pool_create_info, nullptr, &descriptor_pool));

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(desc_layout_bindings.size()),
      .pBindings = desc_layout_bindings.data(),
  };

  VkDescriptorSetLayout descriptor_set_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = static_cast<uint32_t>(num_uniforms ? 1 : 0),
      .pSetLayouts = &descriptor_set_layout,
  };

  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr, &pipeline.layout);

  const std::vector<VkDescriptorSetLayout> descriptor_sets_layouts(kVulkan_NumFramesInFlight, descriptor_set_layout);

  const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = static_cast<uint32_t>(descriptor_sets_layouts.size()),
      .pSetLayouts = descriptor_sets_layouts.data(),
  };

  pipeline.descriptor_sets.resize(kVulkan_NumFramesInFlight);
  VK_CHECK(vkAllocateDescriptorSets(vk.device, &descriptor_set_alloc_info, pipeline.descriptor_sets.data()));

  auto resize = [](auto& b) {
    if (!b.enabled) return;

    b.buffer.resize(kVulkan_NumFramesInFlight);
    b.mem.resize(kVulkan_NumFramesInFlight);
    b.mem_mapped.resize(kVulkan_NumFramesInFlight);
  };

  resize(pipeline.uniform);
  resize(pipeline.dynamic_uniform);
  resize(pipeline.vertex_buffer);

  for (size_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
    if (pipeline.uniform.enabled) {
      RpUniformBuffer& b = pipeline.uniform;
      CreateMappedUniformBuffer(pipeline.descriptor_sets[i], 0, (bool)false, b.size, b.range, b.buffer[i], b.mem[i],
                                reinterpret_cast<void*&>(b.mem_mapped[i]));
    }

    if (pipeline.dynamic_uniform.enabled) {
      RpUniformBuffer& b = pipeline.dynamic_uniform;
      CreateMappedUniformBuffer(pipeline.descriptor_sets[i], 1, true, b.size, b.range, b.buffer[i], b.mem[i],
                                reinterpret_cast<void*&>(b.mem_mapped[i]));
    }

    if (pipeline.vertex_buffer.enabled) {
      RpVertexBuf& b = pipeline.vertex_buffer;
      CreateMappedBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, b.size, b.buffer[i], b.mem[i],
                         reinterpret_cast<void*&>(b.mem_mapped[i]));
    }
  }

  VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  if (pipeline.vertex_buffer.enabled) {
    const VkVertexInputBindingDescription binding_description{
        .binding = 0,
        .stride = CalcVertexBufferStride(pipeline),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    std::vector<VkVertexInputAttributeDescription> vertex_attr_descriptions(pipeline.vertex_buffer.attrs.size());

    {
      uint32_t offset = 0;
      for (uint32_t i = 0; i < pipeline.vertex_buffer.attrs.size(); ++i) {
        auto attr = pipeline.vertex_buffer.attrs[i];
        vertex_attr_descriptions[i] = {
            .location = i,
            .binding = 0,
            .format = RpVertexAttrVkFormat(attr),
            .offset = offset,
        };

        offset += RpVertexAttrSize(attr);
      }
    }

    vertex_input_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_create_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_create_info.vertexAttributeDescriptionCount = vertex_attr_descriptions.size();
    vertex_input_create_info.pVertexAttributeDescriptions = vertex_attr_descriptions.data();

    pipeline.pipeline =
        Vulkan_CreateGraphicsPipeline(vertex_input_create_info, pipeline.layout, pipeline.shader_stages);
  } else {
    pipeline.pipeline =
        Vulkan_CreateGraphicsPipeline(vertex_input_create_info, pipeline.layout, pipeline.shader_stages);
  }
}

void RpBegin(RenderPipeline& pipeline) {
  pipeline.uniform.written_this_frame = 0;
  pipeline.dynamic_uniform.written_this_frame = 0;
  pipeline.vertex_buffer.written_this_frame = 0;

  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
}

void RpSetStaticUniform(RenderPipeline& pipeline, std::span<const char> uniform_data) {
  CHECK(pipeline.uniform.enabled);

  CHECK_LE(uniform_data.size(), pipeline.uniform.size);
  memcpy(pipeline.uniform.mem_mapped[vk.current_frame_data.iframe], uniform_data.data(), uniform_data.size());
}

void RpDraw(RenderPipeline& pipeline, uint32_t vertex_count, std::span<const char> vertex_data,
            std::span<const char> dynamic_uniform_data) {
  CHECK_GT(vertex_count, 0);

  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];

  if (pipeline.vertex_buffer.enabled) {
    auto& written_this_frame = pipeline.vertex_buffer.written_this_frame;
    const auto& buffer = pipeline.vertex_buffer.buffer[vk.current_frame_data.iframe];
    const auto& mem_mapped = pipeline.vertex_buffer.mem_mapped[vk.current_frame_data.iframe];

    const VkDeviceSize offset = pipeline.vertex_buffer.written_this_frame;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &buffer, &offset);

    const uint32_t size = vertex_count * CalcVertexBufferStride(pipeline);
    CHECK_EQ(vertex_data.size(), size);
    CHECK_LE(pipeline.vertex_buffer.written_this_frame + size, pipeline.vertex_buffer.size);

    void* dst = mem_mapped + written_this_frame;
    memcpy(dst, vertex_data.data(), size);
    written_this_frame += size;
  }

  if (pipeline.dynamic_uniform.enabled) {
    auto& written_this_frame = pipeline.dynamic_uniform.written_this_frame;
    written_this_frame = AlignUp(written_this_frame, vk.phys_device_props.limits.minUniformBufferOffsetAlignment);

    const auto& mem_mapped = pipeline.dynamic_uniform.mem_mapped[vk.current_frame_data.iframe];

    const auto descriptor_set = pipeline.descriptor_sets[vk.current_frame_data.iframe];
    const uint32_t dynamic_uniform_offset = pipeline.dynamic_uniform.written_this_frame;
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &descriptor_set, 1,
                            &dynamic_uniform_offset);

    const uint32_t size = pipeline.dynamic_uniform.range;
    CHECK_LE(dynamic_uniform_data.size(), size);
    CHECK_LE(written_this_frame + size, pipeline.dynamic_uniform.size);

    void* dst = mem_mapped + written_this_frame;
    memcpy(dst, dynamic_uniform_data.data(), dynamic_uniform_data.size());
    written_this_frame += size;
  }

  vkCmdDraw(command_buffer, vertex_count, 1, 0, 0);
}

VkFormat RpVertexAttrVkFormat(RpVertexAttr attr) {
  switch (attr) {
    case RpVertexAttr::Float4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RpVertexAttr::Half2:
      return VK_FORMAT_R16G16_SFLOAT;
    case RpVertexAttr::SNorm8x4:
      return VK_FORMAT_R8G8B8A8_SNORM;
    case RpVertexAttr::UNorm8x4:
      return VK_FORMAT_R8G8B8A8_UNORM;
  }

  CHECK(false);
  return VK_FORMAT_UNDEFINED;
}

uint32_t RpVertexAttrSize(RpVertexAttr attr) {
  switch (attr) {
    case RpVertexAttr::Float4:
      return 16;
    case RpVertexAttr::Half2:
      return 4;
    case RpVertexAttr::SNorm8x4:
      return 4;
    case RpVertexAttr::UNorm8x4:
      return 4;
  }

  CHECK(false);
  return 0;
}

}  // namespace nyla