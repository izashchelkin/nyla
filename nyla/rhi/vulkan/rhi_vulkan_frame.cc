#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

RhiCmdList RhiFrameBegin() {
  WaitTimeline(vk.graphics_queue.timeline, vk.graphics_queue_cmd_done[vk.frame_index]);

  VkResult acquire_result =
      vkAcquireNextImageKHR(vk.dev, vk.swapchain, std::numeric_limits<uint64_t>::max(),
                            vk.swapchain_acquire_semaphores[vk.frame_index], VK_NULL_HANDLE, &vk.swapchain_image_index);
  switch (acquire_result) {
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR: {
      vkDeviceWaitIdle(vk.dev);
      CreateSwapchain();

      return RhiFrameBegin();
    }

    default: {
      VK_CHECK(acquire_result);
      break;
    }
  }

  RhiCmdList cmd = vk.graphics_queue_cmd[vk.frame_index];
  VkCommandBuffer cmdbuf = RhiHandleGetData(rhi_handles.cmd_lists, cmd).cmdbuf;

  VK_CHECK(vkResetCommandBuffer(cmdbuf, 0));

  const VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  VK_CHECK(vkBeginCommandBuffer(cmdbuf, &command_buffer_begin_info));

  const VkImageMemoryBarrier image_memory_barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,  // TODO:
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .image = vk.swapchain_images[vk.swapchain_image_index],
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };

  vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &image_memory_barrier);

  const VkRenderingAttachmentInfo color_attachment_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = vk.swapchain_image_views[vk.swapchain_image_index],
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
  };

  const VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, vk.surface_extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_info,
  };

  vkCmdBeginRendering(cmdbuf, &rendering_info);

  const VkViewport viewport{
      .x = 0.f,
      .y = static_cast<float>(vk.surface_extent.height),
      .width = static_cast<float>(vk.surface_extent.width),
      .height = -static_cast<float>(vk.surface_extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

  const VkRect2D scissor{
      .offset = {0, 0},
      .extent = vk.surface_extent,
  };
  vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

  return cmd;
}

void RhiFrameEnd() {
  RhiCmdList cmd = vk.graphics_queue_cmd[vk.frame_index];
  VkCommandBuffer cmdbuf = RhiHandleGetData(rhi_handles.cmd_lists, cmd).cmdbuf;
  vkCmdEndRendering(cmdbuf);

  const VkImageMemoryBarrier image_memory_barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .image = vk.swapchain_images[vk.swapchain_image_index],
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
                       0, nullptr, 0, nullptr, 1, &image_memory_barrier);

  VK_CHECK(vkEndCommandBuffer(cmdbuf));

  const VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  uint64_t& cmd_done = vk.graphics_queue_cmd_done[vk.frame_index];
  cmd_done = vk.graphics_queue.timeline_next++;

  const VkTimelineSemaphoreSubmitInfo timeline_submit_info{
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .signalSemaphoreValueCount = 1,
      .pSignalSemaphoreValues = &cmd_done,
  };

  const VkSemaphore acquire_semaphore = vk.swapchain_acquire_semaphores[vk.frame_index];
  // const VkSemaphore render_finished_semaphore = vk.render_finished_semaphores[vk.frame_index];

  const VkSemaphore signal_semaphores[] = {
      // render_finished_semaphore,
      vk.graphics_queue.timeline,
  };
  const VkSubmitInfo submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = &timeline_submit_info,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &acquire_semaphore,
      .pWaitDstStageMask = wait_stages,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmdbuf,
      .signalSemaphoreCount = std::size(signal_semaphores),
      .pSignalSemaphores = signal_semaphores,
  };
  VK_CHECK(vkQueueSubmit(vk.graphics_queue.queue, 1, &submit_info, VK_NULL_HANDLE));

  const VkTimelineSemaphoreSubmitInfo timeline_wait_info{
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .waitSemaphoreValueCount = 1,
      .pWaitSemaphoreValues = &cmd_done,
  };
  const VkPresentInfoKHR present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = &timeline_wait_info,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &vk.graphics_queue.timeline,
      .swapchainCount = 1,
      .pSwapchains = &vk.swapchain,
      .pImageIndices = &vk.swapchain_image_index,
  };

  const VkResult present_result = vkQueuePresentKHR(vk.graphics_queue.queue, &present_info);
  switch (present_result) {
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR: {
      vkQueueWaitIdle(vk.graphics_queue.queue);
      CreateSwapchain();
      break;
    }

    default: {
      VK_CHECK(present_result);
      break;
    }
  }

  vk.frame_index = (vk.frame_index + 1) % vk.num_frames_in_flight;
}

uint32_t RhiFrameGetIndex() {
  return vk.frame_index;
}

RhiCmdList RhiFrameGetCmdList() {  // TODO: get rid of this
  return vk.graphics_queue_cmd[vk.frame_index];
}

uint32_t RhiGetNumFramesInFlight() {
  return vk.num_frames_in_flight;
}

}  // namespace nyla