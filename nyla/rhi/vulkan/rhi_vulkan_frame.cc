#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

auto RhiFrameBegin() -> RhiCmdList
{
    WaitTimeline(vk.graphicsQueue.timeline, vk.graphicsQueueCmdDone[vk.frameIndex]);

    VkResult acquireResult =
        vkAcquireNextImageKHR(vk.dev, vk.swapchain, std::numeric_limits<uint64_t>::max(),
                              vk.swapchainAcquireSemaphores[vk.frameIndex], VK_NULL_HANDLE, &vk.swapchainTextureIndex);
    switch (acquireResult)
    { // TODO: is drag a problem?
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR: {
        vkDeviceWaitIdle(vk.dev);
        CreateSwapchain();

        return RhiFrameBegin();
    }

    default: {
        VK_CHECK(acquireResult);
        break;
    }
    }

    RhiCmdList cmd = vk.graphicsQueueCmd[vk.frameIndex];
    VkCommandBuffer cmdbuf = RhiHandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;

    VK_CHECK(vkResetCommandBuffer(cmdbuf, 0));

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmdbuf, &commandBufferBeginInfo));

    return cmd;
}

void RhiFrameEnd()
{
    RhiCmdList cmd = vk.graphicsQueueCmd[vk.frameIndex];
    VkCommandBuffer cmdbuf = RhiHandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;

    VK_CHECK(vkEndCommandBuffer(cmdbuf));

    const std::array<VkPipelineStageFlags, 1> waitStages = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSemaphore acquireSemaphore = vk.swapchainAcquireSemaphores[vk.frameIndex];
    const VkSemaphore renderFinishedSemaphore = vk.renderFinishedSemaphores[vk.swapchainTextureIndex];

    const std::array<VkSemaphore, 2> signalSemaphores{
        vk.graphicsQueue.timeline,
        renderFinishedSemaphore,
    };

    vk.graphicsQueueCmdDone[vk.frameIndex] = vk.graphicsQueue.timelineNext++;

    const VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = signalSemaphores.size(),
        .pSignalSemaphoreValues = &vk.graphicsQueueCmdDone[vk.frameIndex],
    };

    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timelineSubmitInfo,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquireSemaphore,
        .pWaitDstStageMask = waitStages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = std::size(signalSemaphores),
        .pSignalSemaphores = signalSemaphores.data(),
    };
    VK_CHECK(vkQueueSubmit(vk.graphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE));

    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &vk.swapchain,
        .pImageIndices = &vk.swapchainTextureIndex,
    };

    const VkResult presentResult = vkQueuePresentKHR(vk.graphicsQueue.queue, &presentInfo);
    switch (presentResult)
    {
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR: {
        vkQueueWaitIdle(vk.graphicsQueue.queue);
        CreateSwapchain();
        break;
    }

    default: {
        VK_CHECK(presentResult);
        break;
    }
    }

    vk.frameIndex = (vk.frameIndex + 1) % vk.numFramesInFlight;
}

auto RhiFrameGetIndex() -> uint32_t
{
    return vk.frameIndex;
}

auto RhiFrameGetCmdList() -> RhiCmdList
{ // TODO: get rid of this
    return vk.graphicsQueueCmd[vk.frameIndex];
}

auto RhiGetNumFramesInFlight() -> uint32_t
{
    return vk.numFramesInFlight;
}

} // namespace nyla