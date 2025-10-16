#include "nyla/vulkan/vulkan.h"

namespace nyla {

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer& buffer,
                  VkDeviceMemory& buffer_memory);

void CreateBuffer(VkCommandPool command_pool, VkQueue transfer_queue,
                  VkDeviceSize data_size, const void* src_data,
                  VkBufferUsageFlags usage, VkBuffer& buffer,
                  VkDeviceMemory& buffer_memory);

}  // namespace nyla
