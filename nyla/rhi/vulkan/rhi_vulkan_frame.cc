#include "nyla/rhi/rhi.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include <limits>

namespace nyla
{

using namespace rhi_vulkan_internal;

auto Rhi::Impl::FrameBegin() -> RhiCmdList
{
    WaitTimeline(m_GraphicsQueue.timeline, m_GraphicsQueueCmdDone[m_FrameIndex]);

    VkResult acquireResult =
        vkAcquireNextImageKHR(m_Dev, m_Swapchain, std::numeric_limits<uint64_t>::max(),
                              m_SwapchainAcquireSemaphores[m_FrameIndex], VK_NULL_HANDLE, &m_SwapchainTextureIndex);
    switch (acquireResult)
    { // TODO: is drag a problem?
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR: {
        vkDeviceWaitIdle(m_Dev);
        CreateSwapchain();

        return FrameBegin();
    }

    default: {
        VK_CHECK(acquireResult);
        break;
    }
    }

    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VK_CHECK(vkResetCommandBuffer(cmdbuf, 0));

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmdbuf, &commandBufferBeginInfo));

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, setIndex, 1, &descriptorSet,
                            dynamicOffsets.size(), dynamicOffsets.data());

    return cmd;
}

void Rhi::Impl::FrameEnd()
{
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VK_CHECK(vkEndCommandBuffer(cmdbuf));

    const std::array<VkPipelineStageFlags, 1> waitStages = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSemaphore acquireSemaphore = m_SwapchainAcquireSemaphores[m_FrameIndex];
    const VkSemaphore renderFinishedSemaphore = m_RenderFinishedSemaphores[m_SwapchainTextureIndex];

    const std::array<VkSemaphore, 2> signalSemaphores{
        m_GraphicsQueue.timeline,
        renderFinishedSemaphore,
    };

    m_GraphicsQueueCmdDone[m_FrameIndex] = m_GraphicsQueue.timelineNext++;

    const VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = signalSemaphores.size(),
        .pSignalSemaphoreValues = &m_GraphicsQueueCmdDone[m_FrameIndex],
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
    VK_CHECK(vkQueueSubmit(m_GraphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE));

    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &m_Swapchain,
        .pImageIndices = &m_SwapchainTextureIndex,
    };

    const VkResult presentResult = vkQueuePresentKHR(m_GraphicsQueue.queue, &presentInfo);
    switch (presentResult)
    {
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR: {
        vkQueueWaitIdle(m_GraphicsQueue.queue);
        CreateSwapchain();
        break;
    }

    default: {
        VK_CHECK(presentResult);
        break;
    }
    }

    m_FrameIndex = (m_FrameIndex + 1) % m_Limits.numFramesInFlight;
}

auto Rhi::Impl::GetFrameIndex() -> uint32_t
{
    return m_FrameIndex;
}

auto Rhi::Impl::FrameGetCmdList() -> RhiCmdList
{ // TODO: get rid of this
    return m_GraphicsQueueCmd[m_FrameIndex];
}

//

auto Rhi::GetNumFramesInFlight() -> uint32_t
{
    return m_Impl->GetNumFramesInFlight();
}

auto Rhi::GetFrameIndex() -> uint32_t
{
    return m_Impl->GetFrameIndex();
}

auto Rhi::FrameBegin() -> RhiCmdList
{
    return m_Impl->FrameBegin();
}

void Rhi::FrameEnd()
{
    m_Impl->FrameEnd();
}

auto Rhi::FrameGetCmdList() -> RhiCmdList
{
    return m_Impl->FrameGetCmdList();
}

} // namespace nyla