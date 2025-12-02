#include <cstdint>

#include "nyla/commons/memory/temp.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace {

VkDescriptorType ConvertVulkanBindingType(RhiBindingType binding_type) {
  switch (binding_type) {
    case RhiBindingType::UniformBuffer:
      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case RhiBindingType::UniformBufferDynamic:
      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  }
  CHECK(false);
  return static_cast<VkDescriptorType>(0);
}

}  // namespace

RhiBindGroupLayout RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc& desc) {
  CHECK_LE(desc.binding_count, std::size(desc.bindings));
  VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[std::size(desc.bindings)];

  for (uint32_t ibinding = 0; ibinding < desc.binding_count; ++ibinding) {
    const auto& binding = desc.bindings[ibinding];
    descriptor_set_layout_bindings[ibinding] = {
        .binding = binding.binding,
        .descriptorType = ConvertVulkanBindingType(binding.type),
        .descriptorCount = binding.array_size,
        .stageFlags = ConvertRhiShaderStageIntoVkShaderStageFlags(binding.stage_flags),
    };
  }

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = desc.binding_count,
      .pBindings = descriptor_set_layout_bindings,
  };

  VkDescriptorSetLayout descriptor_set_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(vk.dev, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

  return RhiHandleAcquire(rhi_handles.bind_group_layouts, descriptor_set_layout);
}

void RhiDestroyBindGroupLayout(RhiBindGroupLayout layout) {
  VkDescriptorSetLayout descriptor_set_layout = RhiHandleRelease(rhi_handles.bind_group_layouts, layout);
  vkDestroyDescriptorSetLayout(vk.dev, descriptor_set_layout, nullptr);
}

RhiBindGroup RhiCreateBindGroup(const RhiBindGroupDesc& desc) {
  VkDescriptorSetLayout descriptor_set_layout = RhiHandleGetData(rhi_handles.bind_group_layouts, desc.layout);

  const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = vk.descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  VkDescriptorSet descriptor_set;
  VK_CHECK(vkAllocateDescriptorSets(vk.dev, &descriptor_set_alloc_info, &descriptor_set));

  VkWriteDescriptorSet writes[std::size(desc.entries)];
  CHECK_LE(desc.entries_count, std::size(desc.entries));

  for (uint32_t i = 0; i < desc.entries_count; ++i) {
    const RhiBindGroupEntry& entry = desc.entries[i];
    switch (entry.type) {
      case RhiBindingType::UniformBuffer:
      case RhiBindingType::UniformBufferDynamic: {
        const VulkanBufferData& buffer_data = RhiHandleGetData(rhi_handles.buffers, entry.buffer.buffer);

        auto buffer_info = &Tmake(VkDescriptorBufferInfo{
            .buffer = buffer_data.buffer,
            .offset = entry.buffer.offset,
            .range = entry.buffer.size,
        });

        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = entry.binding,
            .dstArrayElement = entry.array_index,
            .descriptorCount = 1,
            .descriptorType = ConvertVulkanBindingType(entry.type),
            .pBufferInfo = buffer_info,
        };
        break;
      }

      default: {
        CHECK(false);
      }
    }
  }

  vkUpdateDescriptorSets(vk.dev, desc.entries_count, writes, 0, nullptr);

  return RhiHandleAcquire(rhi_handles.bind_groups, descriptor_set);
}

void RhiDestroyBindGroup(RhiBindGroup bind_group) {
  VkDescriptorSet descriptor_set = RhiHandleRelease(rhi_handles.bind_groups, bind_group);
  vkFreeDescriptorSets(vk.dev, vk.descriptor_pool, 1, &descriptor_set);
}

void RhiCmdBindGraphicsBindGroup(
    RhiCmdList cmd, uint32_t set_index, RhiBindGroup bind_group,
    std::span<const uint32_t> dynamic_offsets) {  // TODO: validate dynamic offsets size !!!
  const VulkanCmdListData& cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);
  const VulkanPipelineData& pipeline_data =
      RhiHandleGetData(rhi_handles.graphics_pipelines, cmd_data.bound_graphics_pipeline);
  const VkDescriptorSet& descriptor_set = RhiHandleGetData(rhi_handles.bind_groups, bind_group);

  vkCmdBindDescriptorSets(cmd_data.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_data.layout, set_index, 1,
                          &descriptor_set, dynamic_offsets.size(), dynamic_offsets.data());
}

}  // namespace nyla