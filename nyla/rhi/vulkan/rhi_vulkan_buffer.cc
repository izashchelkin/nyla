#include <cstdint>

#include "nyla/commons/handle_pool.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla
{

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
    VulkanBufferData bufferData{
        .size = desc.size,
        .memoryUsage = desc.memoryUsage,
    };

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

    return HandleAcquire(rhiHandles.buffers, bufferData);
}

void RhiNameBuffer(RhiBuffer buf, std::string_view name)
{
    VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, buf);
    VulkanNameHandle(VK_OBJECT_TYPE_BUFFER, (uint64_t)bufferData.buffer, name);
}

void RhiDestroyBuffer(RhiBuffer buffer)
{
    VulkanBufferData bufferData = HandleRelease(rhiHandles.buffers, buffer);

    if (bufferData.mapped)
    {
        vkUnmapMemory(vk.dev, bufferData.memory);
    }
    vkDestroyBuffer(vk.dev, bufferData.buffer, nullptr);
    vkFreeMemory(vk.dev, bufferData.memory, nullptr);
}

auto RhiGetBufferSize(RhiBuffer buffer) -> uint32_t
{
    return HandleGetData(rhiHandles.buffers, buffer).size;
}

auto RhiMapBuffer(RhiBuffer buffer) -> char *
{
    VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, buffer);
    if (!bufferData.mapped)
    {
        vkMapMemory(vk.dev, bufferData.memory, 0, VK_WHOLE_SIZE, 0, (void **)&bufferData.mapped);
    }

    return bufferData.mapped;
}

void RhiUnmapBuffer(RhiBuffer buffer)
{
    VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, buffer);
    if (bufferData.mapped)
    {
        vkUnmapMemory(vk.dev, bufferData.memory);
        bufferData.mapped = nullptr;
    }
}

namespace
{

void EnsureHostWritesVisible(VkCommandBuffer cmdbuf, VulkanBufferData &bufferData)
{
    if (bufferData.memoryUsage != RhiMemoryUsage::CpuToGpu)
        return;
    if (!bufferData.dirty)
        return;

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .buffer = bufferData.buffer,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .offset = bufferData.dirtyBegin,
        .size = bufferData.dirtyEnd - bufferData.dirtyBegin,
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    bufferData.dirty = false;
}

} // namespace

void RhiCmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                      uint32_t size)
{
    VkCommandBuffer cmdbuf = HandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;

    VulkanBufferData &dstBufferData = HandleGetData(rhiHandles.buffers, dst);
    VulkanBufferData &srcBufferData = HandleGetData(rhiHandles.buffers, src);

    EnsureHostWritesVisible(cmdbuf, srcBufferData);

    const VkBufferCopy region{
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = size,
    };
    vkCmdCopyBuffer(cmdbuf, srcBufferData.buffer, dstBufferData.buffer, 1, &region);
}

namespace
{

struct VulkanBufferStateSyncInfo
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

auto VulkanBufferStateGetSyncInfo(RhiBufferState state) -> VulkanBufferStateSyncInfo
{
    switch (state)
    {
    case RhiBufferState::Undefined: {
        return {.stage = VK_PIPELINE_STAGE_2_NONE, .access = VK_ACCESS_2_NONE};
    }
    case RhiBufferState::CopySrc: {
        return {.stage = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_READ_BIT};
    }
    case RhiBufferState::CopyDst: {
        return {.stage = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_WRITE_BIT};
    }
    case RhiBufferState::ShaderRead: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    }
    case RhiBufferState::ShaderWrite: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
    }
    case RhiBufferState::Vertex: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    }
    case RhiBufferState::Index: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, .access = VK_ACCESS_2_INDEX_READ_BIT};
    }
    case RhiBufferState::Indirect: {
        return {.stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT};
    }
    }
    CHECK(false);
    return {};
}

} // namespace

void RhiCmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState)
{
    VkCommandBuffer cmdbuf = HandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;
    VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, buffer);

    VulkanBufferStateSyncInfo oldSync = VulkanBufferStateGetSyncInfo(bufferData.state);
    VulkanBufferStateSyncInfo newSync = VulkanBufferStateGetSyncInfo(newState);

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = oldSync.stage,
        .srcAccessMask = oldSync.access,
        .dstStageMask = newSync.stage,
        .dstAccessMask = newSync.access,
        .buffer = bufferData.buffer,
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    bufferData.state = newState;
}

void RhiCmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer)
{
    VkCommandBuffer cmdbuf = HandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;
    VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, buffer);

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = bufferData.buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);
}

void RhiBufferMarkWritten(RhiBuffer buffer, uint32_t offset, uint32_t size)
{
    VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, buffer);

    if (bufferData.dirty)
    {
        bufferData.dirtyBegin = std::min(bufferData.dirtyBegin, offset);
        bufferData.dirtyEnd = std::max(bufferData.dirtyBegin, offset + size);
    }
    else
    {
        bufferData.dirty = true;
        bufferData.dirtyBegin = offset;
        bufferData.dirtyEnd = offset + size;
    }
}

} // namespace nyla