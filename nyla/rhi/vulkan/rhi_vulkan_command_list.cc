#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

namespace
{

auto GetDeviceQueue(RhiQueueType queueType) -> DeviceQueue &
{
    switch (queueType)
    {
    case RhiQueueType::Graphics:
        return vk.graphicsQueue;
    case RhiQueueType::Transfer:
        return vk.transferQueue;
    }
    CHECK(false);
    return vk.graphicsQueue;
}

} // namespace

auto RhiCreateCmdList(RhiQueueType queueType) -> RhiCmdList
{
    VkCommandPool cmdPool = GetDeviceQueue(queueType).cmdPool;
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(vk.dev, &allocInfo, &commandBuffer));

    const VulkanCmdListData cmdData{
        .cmdbuf = commandBuffer,
        .queueType = queueType,
    };

    RhiCmdList cmd = rhiHandles.cmdLists.Acquire(cmdData);
    return cmd;
}

void RhiNameCmdList(RhiCmdList cmd, std::string_view name)
{
    VulkanCmdListData cmdData = rhiHandles.cmdLists.ResolveData(cmd);
    VulkanNameHandle(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmdData.cmdbuf, name);
}

void RhiDestroyCmdList(RhiCmdList cmd)
{
    VulkanCmdListData cmdData = rhiHandles.cmdLists.ReleaseData(cmd);
    VkCommandPool cmdPool = GetDeviceQueue(cmdData.queueType).cmdPool;
    vkFreeCommandBuffers(vk.dev, cmdPool, 1, &cmdData.cmdbuf);
}

auto RhiCmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return data;
    }

    VulkanCmdListData cmdData = rhiHandles.cmdLists.ResolveData(cmd);

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkCmdSetCheckpointNV);
    fn(cmdData.cmdbuf, reinterpret_cast<void *>(data));

    return data;
}

auto RhiGetLastCheckpointData(RhiQueueType queueType) -> uint64_t
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

} // namespace nyla