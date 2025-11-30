#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla {

namespace rhi_vulkan_internal {

VkSemaphore CreateTimeline(uint64_t initial_value) {
  const VkSemaphoreTypeCreateInfo timeline_create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initial_value,
  };

  const VkSemaphoreCreateInfo semaphore_create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timeline_create_info,
  };

  VkSemaphore semaphore;
  vkCreateSemaphore(vk.dev, &semaphore_create_info, nullptr, &semaphore);
  return semaphore;
}

void WaitTimeline(VkSemaphore timeline, uint64_t wait_value) {
  const VkSemaphoreWaitInfo wait_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .pNext = NULL,
      .flags = 0,
      .semaphoreCount = 1,
      .pSemaphores = &timeline,
      .pValues = &wait_value,
  };
  vkWaitSemaphores(vk.dev, &wait_info, std::numeric_limits<uint64_t>::max());
}

}  // namespace rhi_vulkan_internal

}  // namespace nyla