#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

void RhiPassBegin(RhiPassDesc desc)
{
    RhiCmdList cmd = vk.graphicsQueueCmd[vk.frameIndex];
    VkCommandBuffer cmdbuf = RhiHandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;

    const VulkanTextureData colorTargetData = RhiHandleGetData(rhiHandles.textures, desc.colorTarget);

    const VkImageMemoryBarrier imageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // TODO:
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = colorTargetData.image,
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
        .imageView = colorTargetData.imageView,
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
        .renderArea = {{0, 0}, {colorTargetData.extent.width, colorTargetData.extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
    };

    vkCmdBeginRendering(cmdbuf, &renderingInfo);

    const VkViewport viewport{
        .x = 0.f,
        .y = static_cast<float>(colorTargetData.extent.height),
        .width = static_cast<float>(colorTargetData.extent.width),
        .height = -static_cast<float>(colorTargetData.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = {colorTargetData.extent.width, colorTargetData.extent.height},
    };
    vkCmdSetScissor(cmdbuf, 0, 1, &scissor);
}

void RhiPassEnd(RhiPassDesc desc)
{
    RhiCmdList cmd = vk.graphicsQueueCmd[vk.frameIndex];
    VkCommandBuffer cmdbuf = RhiHandleGetData(rhiHandles.cmdLists, cmd).cmdbuf;
    vkCmdEndRendering(cmdbuf);

    const VulkanTextureData colorTargetData = RhiHandleGetData(rhiHandles.textures, desc.colorTarget);

    const VkImageMemoryBarrier imageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = colorTargetData.image,
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
}

} // namespace nyla