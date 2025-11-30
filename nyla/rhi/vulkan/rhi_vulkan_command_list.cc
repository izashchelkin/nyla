#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle_pool.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace {

VkCommandPool GetCmdPool(RhiQueueType queue_type) {
  switch (queue_type) {
    case RhiQueueType::Graphics:
      return vk.graphics_queue.cmd_pool;
    case RhiQueueType::Transfer:
      return vk.transfer_queue.cmd_pool;
  }
  CHECK(false);
}

}  // namespace

RhiCmdList RhiCreateCmdList(RhiQueueType queue_type) {
  VkCommandPool cmd_pool = GetCmdPool(queue_type);
  const VkCommandBufferAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer command_buffer;
  VK_CHECK(vkAllocateCommandBuffers(vk.dev, &alloc_info, &command_buffer));

  const VulkanCmdListData cmd_data{
      .command_buffer = command_buffer,
      .queue_type = queue_type,
  };
  return RhiHandleAcquire(cmd_lists, cmd_data);
}

void RhiDestroyCmdList(RhiCmdList cmd) {
  VulkanCmdListData cmd_data = RhiHandleRelease(cmd_lists, cmd);
  VkCommandPool cmd_pool = GetCmdPool(cmd_data.queue_type);
  vkFreeCommandBuffers(vk.dev, cmd_pool, 1, &cmd_data.command_buffer);
}

}  // namespace nyla