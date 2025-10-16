#include "nyla/vulkan/vulkan.h"

namespace nyla {

inline VkSemaphore CreateSemaphore() {
  const VkSemaphoreCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  VkSemaphore semaphore;
  VK_CHECK(vkCreateSemaphore(vk.device, &create_info, nullptr, &semaphore));
  return semaphore;
}

inline VkFence CreateFence(bool signaled = false) {
  VkFenceCreateFlags flags{};
  if (signaled) flags += VK_FENCE_CREATE_SIGNALED_BIT;

  const VkFenceCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = flags,
  };

  VkFence fence;
  VK_CHECK(vkCreateFence(vk.device, &create_info, nullptr, &fence));
  return fence;
}

}  // namespace nyla
