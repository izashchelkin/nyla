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

    const VulkanTextureData swapchainTextureData =
        RhiHandleGetData(rhiHandles.textures, vk.swapchainTextures[vk.swapchainTextureIndex]);

    const VkImageMemoryBarrier imageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // TODO:
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = swapchainTextureData.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &imageMemoryBarrier);

    const VkRenderingAttachmentInfo colorAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainTextureData.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
    };

    const VkRenderingAttachmentInfo depthAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    };

    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, vk.surfaceExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
    };

    vkCmdBeginRendering(cmdbuf, &renderingInfo);

    const VkViewport viewport{
        .x = 0.f,
        .y = static_cast<float>(vk.surfaceExtent.height),
        .width = static_cast<float>(vk.surfaceExtent.width),
        .height = -static_cast<float>(vk.surfaceExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = vk.surfaceExtent,
    };
    vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

    return cmd;
}

void RhiFrameEnd()
{
    RhiCmdList cmd = vk.graphicsQueueCmd[vk.frameIndex];
    VkCommandBuffer cmdbuf = RhiHandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;
    vkCmdEndRendering(cmdbuf);

    const VulkanTextureData swapchainTextureData =
        RhiHandleGetData(rhiHandles.textures, vk.swapchainTextures[vk.swapchainTextureIndex]);

    const VkImageMemoryBarrier imageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainTextureData.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

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