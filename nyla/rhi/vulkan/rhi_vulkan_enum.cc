#include "nyla/rhi/vulkan/rhi_vulkan_enum.h"

#include "nyla/rhi/rhi.h"

namespace nyla {

VkDescriptorType ConvertVulkanBindingType(RhiBindingType binding_type) {
  switch (binding_type) {
    case RhiBindingType::UniformBuffer:
      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  }
  CHECK(false);
  return static_cast<VkDescriptorType>(0);
}

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

VkFormat ConvertVulkanFormat(RhiFormat format) {
  switch (format) {
    case RhiFormat::Float4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;

    case RhiFormat::Half2:
      return VK_FORMAT_R16G16_SFLOAT;

    case RhiFormat::SNorm8x4:
      return VK_FORMAT_R8G8B8A8_SNORM;

    case RhiFormat::UNorm8x4:
      return VK_FORMAT_R8G8B8A8_UNORM;
  }
  CHECK(false);
  return static_cast<VkFormat>(0);
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

}  // namespace nyla