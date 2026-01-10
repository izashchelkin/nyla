#include <cstdint>

#include "nyla/commons/assert.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

auto Rhi::Impl::CreateTimeline(uint64_t initialValue) -> VkSemaphore
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
    vkCreateSemaphore(m_Dev, &semaphoreCreateInfo, nullptr, &semaphore);
    return semaphore;
}

void Rhi::Impl::WaitTimeline(VkSemaphore timeline, uint64_t waitValue)
{
    const VkSemaphoreWaitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext = NULL,
        .flags = 0,
        .semaphoreCount = 1,
        .pSemaphores = &timeline,
        .pValues = &waitValue,
    };

    VK_CHECK(vkWaitSemaphores(m_Dev, &waitInfo, 1e9));

#if 0
    {
        uint64_t currentValue = -1;
        vkGetSemaphoreCounterValue(m_Dev, timeline, &currentValue);
        DebugBreak();

        VkSemaphoreSignalInfo info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = timeline,
            .value = waitValue,
        };
        VK_CHECK(vkSignalSemaphore(m_Dev, &info));

        vkGetSemaphoreCounterValue(m_Dev, m_GraphicsQueue.timeline, &currentValue);
        DebugBreak();
    }
#endif
}

} // namespace nyla