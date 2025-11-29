#pragma once

#include "nyla/rhi/rhi.h"
#include "vulkan/vulkan_core.h"

namespace nyla {

VkDescriptorType ConvertVulkanBindingType(RhiBindingType);
VkShaderStageFlags ConvertVulkanStageFlags(RhiShaderStage stage_flags);
VkCullModeFlags ConvertVulkanCullMode(RhiCullMode);
VkFrontFace ConvertVulkanFrontFace(RhiFrontFace);
VkCompareOp ConvertVulkanCompareOp(RhiCompareOp);
VkFormat ConvertVulkanFormat(RhiFormat);
VkVertexInputRate ConvertVulkanInputRate(RhiInputRate);

}  // namespace nyla