#include <cstdint>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace
{

DeviceQueue &GetDeviceQueue(RhiQueueType queue_type)
{
    switch (queue_type)
    {
    case RhiQueueType::Graphics:
        return vk.graphics_queue;
    case RhiQueueType::Transfer:
        return vk.transfer_queue;
    }
    CHECK(false);
}

} // namespace

RhiCmdList RhiCreateCmdList(RhiQueueType queue_type)
{
    VkCommandPool cmd_pool = GetDeviceQueue(queue_type).cmd_pool;
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer command_buffer;
    VK_CHECK(vkAllocateCommandBuffers(vk.dev, &alloc_info, &command_buffer));

    const VulkanCmdListData cmd_data{
        .cmdbuf = command_buffer,
        .queue_type = queue_type,
    };

    RhiCmdList cmd = RhiHandleAcquire(rhi_handles.cmd_lists, cmd_data);
    return cmd;
}

void RhiNameCmdList(RhiCmdList cmd, std::string_view name)
{
    VulkanCmdListData cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);
    VulkanNameHandle(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmd_data.cmdbuf, name);
}

void RhiDestroyCmdList(RhiCmdList cmd)
{
    VulkanCmdListData cmd_data = RhiHandleRelease(rhi_handles.cmd_lists, cmd);
    VkCommandPool cmd_pool = GetDeviceQueue(cmd_data.queue_type).cmd_pool;
    vkFreeCommandBuffers(vk.dev, cmd_pool, 1, &cmd_data.cmdbuf);
}

uint64_t __RhiCmdSetCheckpoint(RhiCmdList cmd, uint64_t data)
{
    if constexpr (!rhi_checkpoints)
    {
        return data;
    }

    VulkanCmdListData cmd_data = RhiHandleGetData(rhi_handles.cmd_lists, cmd);

    static PFN_vkCmdSetCheckpointNV fn = VK_GET_INSTANCE_PROC_ADDR(vkCmdSetCheckpointNV);
    fn(cmd_data.cmdbuf, (void *)data);

    return data;
}

uint64_t __RhiGetLastCheckpointData(RhiQueueType queue_type)
{
    if constexpr (!rhi_checkpoints)
    {
        return -1;
    }

    uint32_t data_count = 1;
    VkCheckpointDataNV data{};

    const VkQueue queue = GetDeviceQueue(queue_type).queue;

    static PFN_vkGetQueueCheckpointDataNV fn = VK_GET_INSTANCE_PROC_ADDR(vkGetQueueCheckpointDataNV);
    fn(queue, &data_count, &data);

    return (uint64_t)data.pCheckpointMarker;
}

} // namespace nyla