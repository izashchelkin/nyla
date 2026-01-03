#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include <vulkan/vulkan_core.h>

namespace nyla
{

using namespace rhi_vulkan_internal;

namespace
{

struct VulkanTextureStateSyncInfo
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

auto VulkanTextureStateGetSyncInfo(RhiTextureState state) -> VulkanTextureStateSyncInfo
{
    switch (state)
    {
    case RhiTextureState::Undefined:
        return {
            .stage = VK_PIPELINE_STAGE_2_NONE,
            .access = VK_ACCESS_2_NONE,
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

    case RhiTextureState::ColorTarget:
        return {
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

    case RhiTextureState::DepthTarget:
        return {
            .stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

    case RhiTextureState::ShaderRead:
        return {
            .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

    case RhiTextureState::Present:
        return {
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access = VK_ACCESS_2_NONE,
            .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

    case RhiTextureState::TransferSrc:
        return {
            .stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .access = VK_ACCESS_2_TRANSFER_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        };

    case RhiTextureState::TransferDst:
        return {
            .stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        };
    }
    NYLA_ASSERT(false);
    return {};
}

} // namespace

auto Rhi::Impl::ConvertTextureFormatIntoVkFormat(RhiTextureFormat format) -> VkFormat
{
    switch (format)
    {
    case RhiTextureFormat::None:
        break;
    case RhiTextureFormat::R8G8B8A8_sRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case RhiTextureFormat::B8G8R8A8_sRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case RhiTextureFormat::D32_Float:
        return VK_FORMAT_D32_SFLOAT;
    case RhiTextureFormat::D32_Float_S8_UINT:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    NYLA_ASSERT(false);
    return static_cast<VkFormat>(0);
}

auto Rhi::Impl::ConvertVkFormatIntoTextureFormat(VkFormat format) -> RhiTextureFormat
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_SRGB:
        return RhiTextureFormat::R8G8B8A8_sRGB;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return RhiTextureFormat::B8G8R8A8_sRGB;
    case VK_FORMAT_D32_SFLOAT:
        return RhiTextureFormat::D32_Float;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return RhiTextureFormat::D32_Float_S8_UINT;
    default:
        break;
    }
    NYLA_ASSERT(false);
    return static_cast<RhiTextureFormat>(0);
}

auto Rhi::Impl::ConvertTextureUsageToVkImageUsageFlags(RhiTextureUsage usage) -> VkImageUsageFlags
{
    VkImageUsageFlags flags = 0;

    if (Any(usage & RhiTextureUsage::ShaderSampled))
    {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (Any(usage & RhiTextureUsage::ColorTarget))
    {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (Any(usage & RhiTextureUsage::DepthStencil))
    {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (Any(usage & RhiTextureUsage::TransferSrc))
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & RhiTextureUsage::TransferDst))
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (Any(usage & RhiTextureUsage::Storage))
    {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (Any(usage & RhiTextureUsage::Present))
    {
    }

    return flags;
}

auto Rhi::Impl::CreateTexture(RhiTextureDesc desc) -> RhiTexture
{
    VulkanTextureData textureData{
        .format = ConvertTextureFormatIntoVkFormat(desc.format),
        .extent = {desc.width, desc.height, 1},
    };

    VkMemoryPropertyFlags memoryPropertyFlags = ConvertMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage);

    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureData.format,
        .extent = {desc.width, desc.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = ConvertTextureUsageToVkImageUsageFlags(desc.usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(m_Dev, &imageCreateInfo, m_Alloc, &textureData.image));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(m_Dev, textureData.image, &memoryRequirements);

    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(memoryRequirements, memoryPropertyFlags),
    };
    vkAllocateMemory(m_Dev, &memoryAllocInfo, m_Alloc, &textureData.memory);
    vkBindImageMemory(m_Dev, textureData.image, textureData.memory, 0);

    return m_Textures.Acquire(textureData);
}

auto Rhi::Impl::CreateTextureView(const RhiTextureViewDesc &desc) -> RhiTextureView
{
    VulkanTextureData &textureData = m_Textures.ResolveData(desc.texture);

    // TODO: allow multiple
    NYLA_ASSERT(!HandleIsSet(textureData.view));

    VulkanTextureViewData textureViewData{
        .texture = desc.texture,

        // TODO: expose these as well!
        .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    if (desc.format == RhiTextureFormat::None)
        textureViewData.format = textureData.format;
    else
        textureViewData.format = ConvertTextureFormatIntoVkFormat(desc.format);

    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = textureData.image,
        .viewType = textureViewData.imageViewType,
        .format = textureViewData.format,
        .subresourceRange = textureViewData.subresourceRange,
    };

    VK_CHECK(vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &textureViewData.imageView));

    const RhiTextureView view = m_TextureViews.Acquire(textureViewData);
    textureData.view = view;
    return view;
}

auto Rhi::Impl::GetTextureInfo(RhiTexture texture) -> RhiTextureInfo
{
    const VulkanTextureData textureData = m_Textures.ResolveData(texture);
    return {
        .width = textureData.extent.width,
        .height = textureData.extent.height,
        .format = ConvertVkFormatIntoTextureFormat(textureData.format),
    };
}

void Rhi::Impl::CmdTransitionTexture(RhiCmdList cmd, RhiTexture texture, RhiTextureState newState)
{
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &textureData = m_Textures.ResolveData(texture);

    const VulkanTextureStateSyncInfo newSyncInfo = VulkanTextureStateGetSyncInfo(newState);
    if (newSyncInfo.layout == textureData.layout)
        return;

    const VulkanTextureStateSyncInfo oldSyncInfo = VulkanTextureStateGetSyncInfo(textureData.state);

    const VkImageMemoryBarrier2 imageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = oldSyncInfo.stage,
        .srcAccessMask = oldSyncInfo.access,
        .dstStageMask = newSyncInfo.stage,
        .dstAccessMask = newSyncInfo.access,
        .oldLayout = textureData.layout,
        .newLayout = newSyncInfo.layout,
        .image = textureData.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, // TODO: wtf here
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &imageMemoryBarrier,
    };
    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    textureData.state = newState;
    textureData.layout = newSyncInfo.layout;
}

void Rhi::Impl::DestroyTexture(RhiTexture texture)
{
    VulkanTextureData textureData = m_Textures.ReleaseData(texture);
    NYLA_ASSERT(!textureData.isSwapchain);

    if (HandleIsSet(textureData.view))
    {
        VulkanTextureViewData textureViewData = m_TextureViews.ReleaseData(textureData.view);
        NYLA_ASSERT(textureViewData.texture == texture);

        vkDestroyImageView(m_Dev, textureViewData.imageView, m_Alloc);
    }

    NYLA_ASSERT(textureData.image);
    vkDestroyImage(m_Dev, textureData.image, m_Alloc);

    NYLA_ASSERT(textureData.memory);
    vkFreeMemory(m_Dev, textureData.memory, m_Alloc);
}

void Rhi::Impl::DestroyTextureView(RhiTextureView textureView)
{
    VulkanTextureViewData textureViewData = m_TextureViews.ResolveData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    VulkanTextureData textureData = m_Textures.ReleaseData(textureViewData.texture);
    NYLA_ASSERT(textureData.view == textureView);

    vkDestroyImageView(m_Dev, textureViewData.imageView, m_Alloc);
    textureData.view = {};
}

void Rhi::Impl::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size)
{
    VkCommandBuffer cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &dstTextureData = m_Textures.ResolveData(dst);
    VulkanBufferData &srcBufferData = m_Buffers.ResolveData(src);

    EnsureHostWritesVisible(cmdbuf, srcBufferData);

    const VkBufferImageCopy region{
        .bufferOffset = srcOffset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = dstTextureData.extent,
    };

    vkCmdCopyBufferToImage(cmdbuf, srcBufferData.buffer, dstTextureData.image, dstTextureData.layout, 1, &region);
}

#if 0
        const VkImageCreateInfo depthImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = VkExtent3D{m_SurfaceExtent.width, m_SurfaceExtent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

            // const void*              pNext;
            // VkImageCreateFlags       flags;
            // VkImageType              imageType;
            // VkFormat                 format;
            // VkExtent3D               extent;
            // uint32_t                 mipLevels;
            // uint32_t                 arrayLayers;
            // VkSampleCountFlagBits    samples;
            // VkImageTiling            tiling;
            // VkImageUsageFlags        usage;
            // VkSharingMode            sharingMode;
            // uint32_t                 queueFamilyIndexCount;
            // const uint32_t*          pQueueFamilyIndices;
            // VkImageLayout            initialLayout;

        };

        VkImage depthImage;
        VK_CHECK(vkCreateImage(m_Dev, &depthImageCreateInfo, m_Alloc, &depthImage));

        const VkImageViewCreateInfo depthImageImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VkImageView depthImageView;
        // VK_CHECK(vkCreateImageView(m_Dev, &depthImageImageViewCreateInfo, m_Alloc, &depthImageView));
#endif

//

auto Rhi::CreateTexture(const RhiTextureDesc& desc) -> RhiTexture
{
    return m_Impl->CreateTexture(desc);
}

auto Rhi::CreateTextureView(const RhiTextureViewDesc &desc) -> RhiTextureView
{
    return m_Impl->CreateTextureView(desc);
}

void Rhi::DestroyTexture(RhiTexture texture)
{
    return m_Impl->DestroyTexture(texture);
}

auto Rhi::GetTextureInfo(RhiTexture texture) -> RhiTextureInfo
{
    return m_Impl->GetTextureInfo(texture);
}

void Rhi::CmdTransitionTexture(RhiCmdList cmd, RhiTexture texture, RhiTextureState state)
{
    m_Impl->CmdTransitionTexture(cmd, texture, state);
}

void Rhi::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size)
{
    m_Impl->CmdCopyTexture(cmd, dst, src, srcOffset, size);
}

} // namespace nyla