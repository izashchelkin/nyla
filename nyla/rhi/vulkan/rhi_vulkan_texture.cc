#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include <vulkan/vulkan_core.h>

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal
{

auto ConvertRhiTextureFormatIntoVkFormat(RhiTextureFormat format) -> VkFormat
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
    CHECK(false);
    return static_cast<VkFormat>(0);
}

auto ConvertVkFormatIntoRhiTextureFormat(VkFormat format) -> RhiTextureFormat
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
    CHECK(false);
    return static_cast<RhiTextureFormat>(0);
}

} // namespace rhi_vulkan_internal

auto RhiCreateTexture(RhiTextureDesc desc) -> RhiTexture
{
    VulkanTextureData textureData{
        .format = ConvertRhiTextureFormatIntoVkFormat(desc.format),
    };

    VkMemoryPropertyFlags memoryPropertyFlags = ConvertRhiMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage);

    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureData.format,
        .extent = {desc.width, desc.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(vk.dev, &imageCreateInfo, vk.alloc, &textureData.image));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(vk.dev, textureData.image, &memoryRequirements);

    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(memoryRequirements, memoryPropertyFlags),
    };
    vkAllocateMemory(vk.dev, &memoryAllocInfo, vk.alloc, &textureData.memory);

    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = textureData.image,
        .format = textureData.format,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    vkCreateImageView(vk.dev, &imageViewCreateInfo, vk.alloc, &textureData.imageView);

    return RhiHandleAcquire(rhiHandles.textures, textureData);
}

namespace rhi_vulkan_internal
{

auto RhiCreateTextureFromSwapchainImage(VkImage image, VkSurfaceFormatKHR surfaceFormat, VkExtent2D surfaceExtent)
    -> RhiTexture
{
    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surfaceFormat.format,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkImageView imageView;
    VK_CHECK(vkCreateImageView(vk.dev, &imageViewCreateInfo, vk.alloc, &imageView));

    VulkanTextureData textureData{
        .isSwapchain = true,
        .image = image,
        .imageView = imageView,
        .memory = VK_NULL_HANDLE,
        .format = surfaceFormat.format,
        .extent = {surfaceExtent.width, surfaceExtent.height, 1},
    };

    return RhiHandleAcquire(rhiHandles.textures, textureData);
}

void RhiDestroySwapchainTexture(RhiTexture texture)
{
    VulkanTextureData textureData = RhiHandleRelease(rhiHandles.textures, texture);
    CHECK(textureData.isSwapchain);

    CHECK(textureData.imageView);
    vkDestroyImageView(vk.dev, textureData.imageView, vk.alloc);

    CHECK(textureData.image);
}

} // namespace rhi_vulkan_internal

auto RhiGetTextureInfo(RhiTexture texture) -> RhiTextureInfo
{
    const VulkanTextureData textureData = RhiHandleGetData(rhiHandles.textures, texture);
    return {
        .width = textureData.extent.width,
        .height = textureData.extent.height,
        .format = ConvertVkFormatIntoRhiTextureFormat(textureData.format),
    };
}

void RhiDestroyTexture(RhiTexture texture)
{
    VulkanTextureData textureData = RhiHandleRelease(rhiHandles.textures, texture);
    CHECK(!textureData.isSwapchain);

    CHECK(textureData.imageView);
    vkDestroyImageView(vk.dev, textureData.imageView, vk.alloc);

    CHECK(textureData.image);
    vkDestroyImage(vk.dev, textureData.image, vk.alloc);

    CHECK(textureData.memory);
    vkFreeMemory(vk.dev, textureData.memory, vk.alloc);
}

} // namespace nyla