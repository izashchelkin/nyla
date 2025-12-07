#include <cstdint>

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal
{

auto ConvertRhiBufferUsageIntoVkBufferUsageFlags(RhiBufferUsage usage) -> VkBufferUsageFlags
{
    VkBufferUsageFlags ret = 0;

    if (Any(usage & RhiBufferUsage::Vertex))
    {
        ret |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (Any(usage & RhiBufferUsage::Index))
    {
        ret |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (Any(usage & RhiBufferUsage::Uniform))
    {
        ret |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (Any(usage & RhiBufferUsage::TransferSrc))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & RhiBufferUsage::TransferDst))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return ret;
}

auto ConvertRhiMemoryUsageIntoVkMemoryPropertyFlags(RhiMemoryUsage usage) -> VkMemoryPropertyFlags
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    switch (usage)
    {
    case nyla::RhiMemoryUsage::Unknown:
        break;
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

auto FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    static const VkPhysicalDeviceMemoryProperties memPropertities = [] -> VkPhysicalDeviceMemoryProperties {
        VkPhysicalDeviceMemoryProperties memPropertities;
        vkGetPhysicalDeviceMemoryProperties(vk.physDev, &memPropertities);
        return memPropertities;
    }();

    for (uint32_t i = 0; i < memPropertities.memoryTypeCount; ++i)
    {
        if (!(memRequirements.memoryTypeBits & (1 << i)))
        {
            continue;
        }

        if ((memPropertities.memoryTypes[i].propertyFlags & properties) != properties)
        {
            continue;
        }

        return i;
    }

    CHECK(false);
}

} // namespace rhi_vulkan_internal

auto RhiCreateBuffer(const RhiBufferDesc &desc) -> RhiBuffer
{
    VulkanBufferData bufferData{};

    const VkBufferCreateInfo bufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = ConvertRhiBufferUsageIntoVkBufferUsageFlags(desc.bufferUsage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(vk.dev, &bufferCreateInfo, nullptr, &bufferData.buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(rhi_vulkan_internal::vk.dev, bufferData.buffer, &memRequirements);

    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(memRequirements, ConvertRhiMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage));
    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    VK_CHECK(vkAllocateMemory(vk.dev, &memoryAllocInfo, nullptr, &bufferData.memory));
    VK_CHECK(vkBindBufferMemory(vk.dev, bufferData.buffer, bufferData.memory, 0));

    return RhiHandleAcquire(rhiHandles.buffers, bufferData);
}

void RhiNameBuffer(RhiBuffer buf, std::string_view name)
{
    VulkanBufferData &bufferData = RhiHandleGetData(rhiHandles.buffers, buf);
    VulkanNameHandle(VK_OBJECT_TYPE_BUFFER, (uint64_t)bufferData.buffer, name);
}

void RhiDestroyBuffer(RhiBuffer buffer)
{
    VulkanBufferData bufferData = RhiHandleRelease(rhiHandles.buffers, buffer);

    if (bufferData.mapped)
    {
        vkUnmapMemory(vk.dev, bufferData.memory);
    }
    vkDestroyBuffer(vk.dev, bufferData.buffer, nullptr);
    vkFreeMemory(vk.dev, bufferData.memory, nullptr);
}

auto RhiMapBuffer(RhiBuffer buffer, bool idempotent) -> void *
{
    VulkanBufferData &bufferData = RhiHandleGetData(rhiHandles.buffers, buffer);
    if (!bufferData.mapped)
    {
        vkMapMemory(vk.dev, bufferData.memory, 0, VK_WHOLE_SIZE, 0, (void **)&bufferData.mapped);
    }
    else
    {
        CHECK(idempotent);
    }

    return bufferData.mapped;
}

void RhiUnmapBuffer(RhiBuffer buffer, bool idempotent)
{
    VulkanBufferData &bufferData = RhiHandleGetData(rhiHandles.buffers, buffer);
    if (bufferData.mapped)
    {
        vkUnmapMemory(vk.dev, bufferData.memory);
        bufferData.mapped = nullptr;
    }
    else
    {
        CHECK(idempotent);
    }
}

} // namespace nyla