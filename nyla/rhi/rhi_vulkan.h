#pragma once

#include "nyla/rhi/rhi.h"
#include "vulkan/vulkan_core.h"

namespace nyla {

VkDescriptorType ConvertVulkanBindingType(RhiBindingType binding_type);
VkShaderStageFlags ConvertVulkanStageFlags(uint32_t stage_flags);
VkCullModeFlags ConvertVulkanCullMode(RhiCullMode cull_mode);
VkFrontFace ConvertVulkanFrontFace(RhiFrontFace front_face);
VkCompareOp ConvertVulkanCompareOp(RhiCompareOp compare_op);
VkFormat ConvertVulkanFormat(RhiCompareOp compare_op);

}  // namespace nyla