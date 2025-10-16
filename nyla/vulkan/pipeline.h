#include <vector>

#include "nyla/vulkan/vulkan.h"
#include "vulkan/vulkan.h"

namespace nyla {

inline VkShaderModule CreateShaderModule(const std::vector<char>& code) {
  const VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };

  VkShaderModule shader_module;
  VK_CHECK(
      vkCreateShaderModule(vk.device, &create_info, nullptr, &shader_module));
  return shader_module;
}

}  // namespace nyla
