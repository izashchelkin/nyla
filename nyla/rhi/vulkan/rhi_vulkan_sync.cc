#include <cstdint>

#include "nyla/commons/debug/debugger.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

namespace rhi_vulkan_internal
{

auto CreateTimeline(uint64_t initialValue) -> VkSemaphore
{
    const VkSemaphoreTypeCreateInfo timelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = initialValue,
    };

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timelineCreateInfo,
    };

    VkSemaphore semaphore;
    vkCreateSemaphore(vk.dev, &semaphoreCreateInfo, nullptr, &semaphore);
    return semaphore;
}

void WaitTimeline(VkSemaphore timeline, uint64_t waitValue)
{
    const VkSemaphoreWaitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext = NULL,
        .flags = 0,
        .semaphoreCount = 1,
        .pSemaphores = &timeline,
        .pValues = &waitValue,
    };

    if (vkWaitSemaphores(vk.dev, &waitInfo, 1e9) != VK_SUCCESS)
    {
        uint64_t currentValue = -1;
        vkGetSemaphoreCounterValue(vk.dev, timeline, &currentValue);
        DebugBreak;

        VkSemaphoreSignalInfo info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = timeline,
            .value = waitValue,
        };
        VK_CHECK(vkSignalSemaphore(vk.dev, &info));

        vkGetSemaphoreCounterValue(vk.dev, vk.graphicsQueue.timeline, &currentValue);
        DebugBreak;
    }
}

} // namespace rhi_vulkan_internal

} // namespace nyla