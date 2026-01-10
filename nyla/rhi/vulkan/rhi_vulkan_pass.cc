#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

void Rhi::Impl::PassBegin(RhiPassDesc desc)
{
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    const VulkanTextureViewData &colorTargetViewData = m_RenderTargetViews.ResolveData(desc.renderTarget);
    const VulkanTextureData &colorTargetData = m_Textures.ResolveData(colorTargetViewData.texture);

    CmdTransitionTexture(cmd, colorTargetViewData.texture, desc.state);

    const VkRenderingAttachmentInfo colorAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = colorTargetViewData.imageView,
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

void Rhi::Impl::PassEnd(RhiPassDesc desc)
{
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    vkCmdEndRendering(cmdbuf);

    const VulkanTextureViewData &rtvData = m_RenderTargetViews.ResolveData(desc.renderTarget);

    CmdTransitionTexture(cmd, rtvData.texture, desc.state);

    cmdData.passConstantHead += CbvOffset(m_Limits.passConstantSize);
}

//

void Rhi::PassBegin(RhiPassDesc desc)
{
    m_Impl->PassBegin(desc);
}

void Rhi::PassEnd(RhiPassDesc desc)
{
    m_Impl->PassEnd(desc);
}

} // namespace nyla