#include "nyla/vulkan/memory.h"

#include "nyla/vulkan/vulkan.h"

namespace nyla {

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer& buffer,
                  VkDeviceMemory& buffer_memory) {
  const VkBufferCreateInfo buffer_create_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VK_CHECK(vkCreateBuffer(vk.device, &buffer_create_info, nullptr, &buffer));

  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(vk.device, buffer, &mem_requirements);

  const VkMemoryAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_requirements.size,
      .memoryTypeIndex =
          [=]() {
            VkPhysicalDeviceMemoryProperties mem_propertities;
            vkGetPhysicalDeviceMemoryProperties(vk.phys_device,
                                                &mem_propertities);

            for (uint32_t i = 0; i < mem_propertities.memoryTypeCount; ++i) {
              if (!(mem_requirements.memoryTypeBits & (1 << i))) {
                continue;
              }
              if ((mem_propertities.memoryTypes[i].propertyFlags &
                   properties) != properties) {
                continue;
              }
              return i;
            }

            CHECK(false);
          }(),
  };

  VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &buffer_memory));
  VK_CHECK(vkBindBufferMemory(vk.device, buffer, buffer_memory, 0));
}

void CreateBuffer(VkCommandPool command_pool, VkQueue transfer_queue,
                  VkDeviceSize data_size, const void* src_data,
                  VkBufferUsageFlags usage, VkBuffer& buffer,
                  VkDeviceMemory& buffer_memory) {
  VkBuffer staging_buffer;
  VkDeviceMemory staging_buffer_memory;
  CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               staging_buffer, staging_buffer_memory);

  void* dst_data;
  vkMapMemory(vk.device, staging_buffer_memory, 0, data_size, 0, &dst_data);
  memcpy(dst_data, src_data, data_size);
  vkUnmapMemory(vk.device, staging_buffer_memory);

  CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, buffer_memory);

  const VkCommandBufferAllocateInfo command_buffer_alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer command_buffer;
  VK_CHECK(vkAllocateCommandBuffers(vk.device, &command_buffer_alloc_info,
                                    &command_buffer));

  const VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };

  VK_CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

  const VkBufferCopy copy_region{
      .srcOffset = 0,
      .dstOffset = 0,
      .size = data_size,
  };
  vkCmdCopyBuffer(command_buffer, staging_buffer, buffer, 1, &copy_region);

  vkEndCommandBuffer(command_buffer);

  const VkSubmitInfo submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
  };
  vkQueueSubmit(transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(transfer_queue);
  vkFreeCommandBuffers(vk.device, command_pool, 1, &command_buffer);
  vkDestroyBuffer(vk.device, staging_buffer, nullptr);
  vkFreeMemory(vk.device, staging_buffer_memory, nullptr);
}

}  // namespace nyla
