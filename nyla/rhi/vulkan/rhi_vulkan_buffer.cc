#include <cstdint>

#include "nyla/commons/handle_pool.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

auto Rhi::Impl::ConvertBufferUsageIntoVkBufferUsageFlags(RhiBufferUsage usage) -> VkBufferUsageFlags
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
    if (Any(usage & RhiBufferUsage::CopySrc))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & RhiBufferUsage::CopyDst))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return ret;
}

auto Rhi::Impl::ConvertMemoryUsageIntoVkMemoryPropertyFlags(RhiMemoryUsage usage) -> VkMemoryPropertyFlags
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
    NYLA_ASSERT(false);
    return 0;
}

auto Rhi::Impl::FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    static const VkPhysicalDeviceMemoryProperties memPropertities = [this] -> VkPhysicalDeviceMemoryProperties {
        VkPhysicalDeviceMemoryProperties memPropertities;
        vkGetPhysicalDeviceMemoryProperties(m_PhysDev, &memPropertities);
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

    NYLA_ASSERT(false);
    return 0;
}

auto Rhi::Impl::CreateBuffer(const RhiBufferDesc &desc) -> RhiBuffer
{
    VulkanBufferData bufferData{
        .size = desc.size,
        .memoryUsage = desc.memoryUsage,
    };

    const VkBufferCreateInfo bufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = ConvertBufferUsageIntoVkBufferUsageFlags(desc.bufferUsage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(m_Dev, &bufferCreateInfo, nullptr, &bufferData.buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Dev, bufferData.buffer, &memRequirements);

    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(memRequirements, ConvertMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage));
    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    VK_CHECK(vkAllocateMemory(m_Dev, &memoryAllocInfo, nullptr, &bufferData.memory));
    VK_CHECK(vkBindBufferMemory(m_Dev, bufferData.buffer, bufferData.memory, 0));

    return m_Buffers.Acquire(bufferData);
}

void Rhi::Impl::NameBuffer(RhiBuffer buf, std::string_view name)
{
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buf);
    VulkanNameHandle(VK_OBJECT_TYPE_BUFFER, (uint64_t)bufferData.buffer, name);
}

void Rhi::Impl::DestroyBuffer(RhiBuffer buffer)
{
    VulkanBufferData bufferData = m_Buffers.ReleaseData(buffer);

    if (bufferData.mapped)
    {
        vkUnmapMemory(m_Dev, bufferData.memory);
    }
    vkDestroyBuffer(m_Dev, bufferData.buffer, nullptr);
    vkFreeMemory(m_Dev, bufferData.memory, nullptr);
}

auto Rhi::Impl::GetBufferSize(RhiBuffer buffer) -> uint32_t
{
    return m_Buffers.ResolveData(buffer).size;
}

auto Rhi::Impl::MapBuffer(RhiBuffer buffer) -> char *
{
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);
    if (!bufferData.mapped)
    {
        vkMapMemory(m_Dev, bufferData.memory, 0, VK_WHOLE_SIZE, 0, (void **)&bufferData.mapped);
    }

    return bufferData.mapped;
}

void Rhi::Impl::UnmapBuffer(RhiBuffer buffer)
{
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);
    if (bufferData.mapped)
    {
        vkUnmapMemory(m_Dev, bufferData.memory);
        bufferData.mapped = nullptr;
    }
}

void Rhi::Impl::EnsureHostWritesVisible(VkCommandBuffer cmdbuf, VulkanBufferData &bufferData)
{
    if (bufferData.memoryUsage != RhiMemoryUsage::CpuToGpu)
        return;
    if (!bufferData.dirty)
        return;

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = bufferData.buffer,
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

void Rhi::Impl::CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                              uint32_t size)
{
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanBufferData &dstBufferData = m_Buffers.ResolveData(dst);
    VulkanBufferData &srcBufferData = m_Buffers.ResolveData(src);

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
    NYLA_ASSERT(false);
    return {};
}

} // namespace

void Rhi::Impl::CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState)
{
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

    VulkanBufferStateSyncInfo oldSync = VulkanBufferStateGetSyncInfo(bufferData.state);
    VulkanBufferStateSyncInfo newSync = VulkanBufferStateGetSyncInfo(newState);

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = oldSync.stage,
        .srcAccessMask = oldSync.access,
        .dstStageMask = newSync.stage,
        .dstAccessMask = newSync.access,
        .buffer = bufferData.buffer,
        .size = GetBufferSize(buffer),
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    bufferData.state = newState;
}

void Rhi::Impl::CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer)
{
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

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

void Rhi::Impl::BufferMarkWritten(RhiBuffer buffer, uint32_t offset, uint32_t size)
{
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

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

//

auto Rhi::CreateBuffer(const RhiBufferDesc &desc) -> RhiBuffer
{
    return m_Impl->CreateBuffer(desc);
}

void Rhi::NameBuffer(RhiBuffer buffer, std::string_view name)
{
    m_Impl->NameBuffer(buffer, name);
}

void Rhi::DestroyBuffer(RhiBuffer buffer)
{
    m_Impl->DestroyBuffer(buffer);
}

auto Rhi::GetBufferSize(RhiBuffer buffer) -> uint32_t
{
    return m_Impl->GetBufferSize(buffer);
}

auto Rhi::MapBuffer(RhiBuffer buffer) -> char *
{
    return m_Impl->MapBuffer(buffer);
}

void Rhi::UnmapBuffer(RhiBuffer buffer)
{
    return m_Impl->UnmapBuffer(buffer);
}

void Rhi::BufferMarkWritten(RhiBuffer buffer, uint32_t offset, uint32_t size)
{
    m_Impl->BufferMarkWritten(buffer, offset, size);
}

void Rhi::CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                        uint32_t size)
{
    m_Impl->CmdCopyBuffer(cmd, dst, dstOffset, src, srcOffset, size);
}

void Rhi::CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState)
{
    m_Impl->CmdTransitionBuffer(cmd, buffer, newState);
}

void Rhi::CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer)
{
    m_Impl->CmdUavBarrierBuffer(cmd, buffer);
}

} // namespace nyla