#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle_pool.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla {

namespace {

VkShaderStageFlags ConvertVulkanStageFlags(RhiShaderStage stage_flags) {
  VkShaderStageFlags ret = 0;
  if (Any(stage_flags & RhiShaderStage::Vertex)) {
    ret |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if (Any(stage_flags & RhiShaderStage::Fragment)) {
    ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  return ret;
}

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

namespace rhi_vulkan_internal {

rhi_internal::RhiHandlePool<VkDescriptorSetLayout, 64> bind_group_layouts;

}

RhiBindGroupLayout RhiCreateBindGroupLayout(RhiBindGroupLayoutDesc desc) {
  using namespace rhi_vulkan_internal;

  CHECK_LE(desc.binding_count, std::size(desc.bindings));
  VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[std::size(desc.bindings)];

  for (uint32_t ibinding = 0; ibinding < desc.binding_count; ++ibinding) {
    const auto& binding = desc.bindings[ibinding];
    descriptor_set_layout_bindings[ibinding] = {
        .binding = binding.binding,
        .descriptorType = ConvertVulkanBindingType(binding.type),
        .descriptorCount = binding.array_size,
        .stageFlags = ConvertVulkanStageFlags(binding.stage_flags),
    };
  }

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = desc.binding_count,
      .pBindings = descriptor_set_layout_bindings,
  };

  VkDescriptorSetLayout descriptor_set_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(vk.dev, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

  return static_cast<RhiBindGroupLayout>(RhiHandleAcquire(bind_group_layouts, descriptor_set_layout));
}

void RhiDestroyBindGroupLayout(RhiBindGroupLayout layout) {
  using namespace rhi_vulkan_internal;

  VkDescriptorSetLayout descriptor_set_layout = RhiHandleRelease(bind_group_layouts, layout);
  vkDestroyDescriptorSetLayout(vk.dev, descriptor_set_layout, nullptr);
}

}  // namespace nyla