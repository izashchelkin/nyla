#include "nyla/rhi/rhi.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include <limits>

namespace nyla
{

using namespace rhi_vulkan_internal;

auto Rhi::Impl::FrameBegin() -> RhiCmdList
{
    if (m_RecreateSwapchain)
    {
        vkDeviceWaitIdle(m_Dev);
        CreateSwapchain();
        m_SwapchainUsable = true;
        m_RecreateSwapchain = false;
    }

    if (m_SwapchainUsable)
        WaitTimeline(m_GraphicsQueue.timeline, m_GraphicsQueueCmdDone[m_FrameIndex]);
    else
        vkDeviceWaitIdle(m_Dev);

    VkResult acquireResult =
        vkAcquireNextImageKHR(m_Dev, m_Swapchain, std::numeric_limits<uint64_t>::max(),
                              m_SwapchainAcquireSemaphores[m_FrameIndex], VK_NULL_HANDLE, &m_SwapchainTextureIndex);
    switch (acquireResult)
    {
    case VK_ERROR_OUT_OF_DATE_KHR: {
        m_SwapchainUsable = false;
        break;
    }

    case VK_SUBOPTIMAL_KHR: {
        m_SwapchainUsable = true;
        break;
    }

    default: {
        m_SwapchainUsable = true;
        VK_CHECK(acquireResult);
        break;
    }
    }

    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    ResetCmdList(cmd);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmdbuf, &commandBufferBeginInfo));

    return cmd;
}

void Rhi::Impl::FrameEnd()
{
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VK_CHECK(vkEndCommandBuffer(cmdbuf));

    WriteDescriptorTables();

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

    if (m_SwapchainUsable)
    {
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
    }

    m_FrameIndex = (m_FrameIndex + 1) % m_Limits.numFramesInFlight;
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