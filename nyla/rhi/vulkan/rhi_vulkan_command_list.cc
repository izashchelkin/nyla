#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

auto Rhi::Impl::GetDeviceQueue(RhiQueueType queueType) -> DeviceQueue &
{
    switch (queueType)
    {
    case RhiQueueType::Graphics:
        return m_GraphicsQueue;
    case RhiQueueType::Transfer:
        return m_TransferQueue;
    }
    NYLA_ASSERT(false);
    return m_GraphicsQueue;
}

auto Rhi::Impl::CreateCmdList(RhiQueueType queueType) -> RhiCmdList
{
    VkCommandPool cmdPool = GetDeviceQueue(queueType).cmdPool;
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(m_Dev, &allocInfo, &commandBuffer));

    const VulkanCmdListData cmdData{
        .cmdbuf = commandBuffer,
        .queueType = queueType,
    };

    RhiCmdList cmd = m_CmdLists.Acquire(cmdData);
    return cmd;
}

void Rhi::Impl::NameCmdList(RhiCmdList cmd, std::string_view name)
{
    VulkanCmdListData cmdData = m_CmdLists.ResolveData(cmd);
    VulkanNameHandle(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmdData.cmdbuf, name);
}

void Rhi::Impl::DestroyCmdList(RhiCmdList cmd)
{
    VulkanCmdListData cmdData = m_CmdLists.ReleaseData(cmd);
    VkCommandPool cmdPool = GetDeviceQueue(cmdData.queueType).cmdPool;
    vkFreeCommandBuffers(m_Dev, cmdPool, 1, &cmdData.cmdbuf);
}

auto Rhi::Impl::CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return data;
    }

    VulkanCmdListData cmdData = m_CmdLists.ResolveData(cmd);

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkCmdSetCheckpointNV);
    fn(cmdData.cmdbuf, reinterpret_cast<void *>(data));

    return data;
}

auto Rhi::Impl::GetLastCheckpointData(RhiQueueType queueType) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return -1;
    }

    uint32_t dataCount = 1;
    VkCheckpointDataNV data{};

    const VkQueue queue = GetDeviceQueue(queueType).queue;

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkGetQueueCheckpointDataNV);
    fn(queue, &dataCount, &data);

    return (uint64_t)data.pCheckpointMarker;
}

//

auto Rhi::CreateCmdList(RhiQueueType queueType) -> RhiCmdList
{
    return m_Impl->CreateCmdList(queueType);
}

void Rhi::NameCmdList(RhiCmdList cmd, std::string_view name)
{
    m_Impl->NameCmdList(cmd, name);
}

void Rhi::DestroyCmdList(RhiCmdList cmd)
{
    m_Impl->DestroyCmdList(cmd);
}

auto Rhi::CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t
{
    return m_Impl->CmdSetCheckpoint(cmd, data);
}

auto Rhi::GetLastCheckpointData(RhiQueueType queueType) -> uint64_t
{
    return m_Impl->GetLastCheckpointData(queueType);
}

} // namespace nyla