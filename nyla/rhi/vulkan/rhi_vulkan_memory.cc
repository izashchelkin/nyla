#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle_pool.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal {

RhiHandlePool<VulkanBufferData, 16> buffers;

}

namespace {

VkBufferUsageFlags ConvertVulkanBufferUsage(RhiBufferUsage usage) {
  VkBufferUsageFlags ret = 0;

  if (Any(usage & RhiBufferUsage::Vertex)) {
    ret |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (Any(usage & RhiBufferUsage::Index)) {
    ret |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (Any(usage & RhiBufferUsage::Uniform)) {
    ret |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (Any(usage & RhiBufferUsage::TransferSrc)) {
    ret |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if (Any(usage & RhiBufferUsage::TransferDst)) {
    ret |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  return ret;
}

VkMemoryPropertyFlags ConvertVulkanMemoryUsage(RhiMemoryUsage usage) {
  // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

  switch (usage) {
    case RhiMemoryUsage::GpuOnly:
      return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case RhiMemoryUsage::CpuToGpu:
      return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case RhiMemoryUsage::GpuToCpu:
      return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
             VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }
  CHECK(false);
  return 0;
}

uint32_t FindMemoryTypeIndex(VkMemoryRequirements mem_requirements, VkMemoryPropertyFlags properties) {
  using namespace rhi_vulkan_internal;

  // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

  static const VkPhysicalDeviceMemoryProperties mem_propertities = [] {
    VkPhysicalDeviceMemoryProperties mem_propertities;
    vkGetPhysicalDeviceMemoryProperties(vk.phys_dev, &mem_propertities);
    return mem_propertities;
  }();

  for (uint32_t i = 0; i < mem_propertities.memoryTypeCount; ++i) {
    if (!(mem_requirements.memoryTypeBits & (1 << i))) {
      continue;
    }

    if ((mem_propertities.memoryTypes[i].propertyFlags & properties) != properties) {
      continue;
    }

    return i;
  }

  CHECK(false);
}

}  // namespace

RhiBuffer RhiCreateBuffer(RhiBufferDesc desc) {
  using namespace rhi_vulkan_internal;

  VulkanBufferData buffer_data{};

  const VkBufferCreateInfo buffer_create_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .usage = ConvertVulkanBufferUsage(desc.buffer_usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  VK_CHECK(vkCreateBuffer(vk.dev, &buffer_create_info, nullptr, &buffer_data.buffer));

  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(rhi_vulkan_internal::vk.dev, buffer_data.buffer, &mem_requirements);

  const uint32_t memory_type_index = FindMemoryTypeIndex(mem_requirements, ConvertVulkanMemoryUsage(desc.memory_usage));
  const VkMemoryAllocateInfo memory_alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_requirements.size,
      .memoryTypeIndex = memory_type_index,
  };

  VK_CHECK(vkAllocateMemory(vk.dev, &memory_alloc_info, nullptr, &buffer_data.memory));
  VK_CHECK(vkBindBufferMemory(vk.dev, buffer_data.buffer, buffer_data.memory, 0));

  return RhiHandleAcquire(buffers, buffer_data);
}

void RhiDestroyBuffer(RhiBuffer buffer) {
  using namespace rhi_vulkan_internal;

  VulkanBufferData buffer_data = RhiHandleRelease(buffers, buffer);

  if (buffer_data.mapped) {
    vkUnmapMemory(vk.dev, buffer_data.memory);
  }
  vkDestroyBuffer(vk.dev, buffer_data.buffer, nullptr);
  vkFreeMemory(vk.dev, buffer_data.memory, nullptr);
}

void* RhiMapBuffer(RhiBuffer buffer, bool idempotent) {
  using namespace rhi_vulkan_internal;

  VulkanBufferData& buffer_data = RhiHandleGetData(buffers, buffer);
  if (!buffer_data.mapped) {
    vkMapMemory(vk.dev, buffer_data.memory, 0, VK_WHOLE_SIZE, 0, (void**)&buffer_data.mapped);
  } else {
    CHECK(idempotent);
  }

  return buffer_data.mapped;
}

void RhiUnmapBuffer(RhiBuffer buffer, bool idempotent) {
  using namespace rhi_vulkan_internal;

  VulkanBufferData& buffer_data = RhiHandleGetData(buffers, buffer);
  if (buffer_data.mapped) {
    vkUnmapMemory(vk.dev, buffer_data.memory);
    buffer_data.mapped = nullptr;
  } else {
    CHECK(idempotent);
  }
}

}  // namespace nyla