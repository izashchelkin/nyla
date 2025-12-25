#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

void RhiPassBegin(RhiPassDesc desc)
{
    RhiCmdList cmd = vk.graphicsQueueCmd[vk.frameIndex];
    VkCommandBuffer cmdbuf = rhiHandles.cmdLists.ResolveData(cmd).cmdbuf;

    RhiCmdTransitionTexture(cmd, desc.colorTarget, desc.state);

    const VulkanTextureData colorTargetData = rhiHandles.textures.ResolveData(desc.colorTarget);

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
    VkCommandBuffer cmdbuf = rhiHandles.cmdLists.ResolveData(cmd).cmdbuf;
    vkCmdEndRendering(cmdbuf);

    RhiCmdTransitionTexture(cmd, desc.colorTarget, desc.state);
}

} // namespace nyla