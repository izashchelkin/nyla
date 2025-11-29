#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_vulkan.h"

namespace nyla {

VkDescriptorType ConvertVulkanBindingType(RhiBindingType binding_type) {
  switch (binding_type) {
    case RhiBindingType::UniformBuffer:
      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  }
  CHECK(false);
}

VkShaderStageFlags ConvertVulkanStageFlags(uint32_t stage_flags) {
  VkShaderStageFlags ret = 0;

  if (stage_flags & static_cast<uint32_t>(RhiShaderType::Vertex)) {
    ret |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if (stage_flags & static_cast<uint32_t>(RhiShaderType::Fragment)) {
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
}

VkFrontFace ConvertVulkanFrontFace(RhiFrontFace front_face) {
  switch (front_face) {
    case RhiFrontFace::CCW:
      return VK_FRONT_FACE_COUNTER_CLOCKWISE;

    case RhiFrontFace::CW:
      return VK_FRONT_FACE_CLOCKWISE;
  }
  CHECK(false);
}

VkCompareOp ConvertVulkanCompareOp(RhiCompareOp compare_op) {
  switch (compare_op) {
    case RhiCompareOp::Always:
      return VK_COMPARE_OP_ALWAYS;

    case RhiCompareOp::Greater:
      return VK_COMPARE_OP_GREATER;

    case RhiCompareOp::Less:
      return VK_COMPARE_OP_LESS;

    case RhiCompareOp::LessEqual:
      return VK_COMPARE_OP_LESS_OR_EQUAL;
  }
  CHECK(false);
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
}

}  // namespace nyla