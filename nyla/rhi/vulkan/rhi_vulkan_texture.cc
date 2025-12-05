#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

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
    case RhiTextureFormat::R8G8B8A8SRgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    }

    CHECK(false);
    return static_cast<VkFormat>(0);
}

auto ConvertVkFormatIntoRhiTextureFormat(VkFormat format) -> RhiTextureFormat
{
    CHECK(false);
    return static_cast<RhiTextureFormat>(0);
}

} // namespace rhi_vulkan_internal

auto RhiCreateTexture(RhiTextureDesc desc) -> RhiTexture
{
    VulkanTextureData textureData{
        .format = ConvertRhiTextureFormatIntoVkFormat(desc.format),
        .memoryPropertyFlags = ConvertRhiMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage),
    };

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
        .memoryTypeIndex = FindMemoryTypeIndex(memoryRequirements, textureData.memoryPropertyFlags),
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

void RhiDestroyTexture(RhiTexture texture)
{
    VulkanTextureData textureData = RhiHandleRelease(rhiHandles.textures, texture);

    vkDestroyImageView(vk.dev, textureData.imageView, vk.alloc);
    vkDestroyImage(vk.dev, textureData.image, vk.alloc);
    vkFreeMemory(vk.dev, textureData.memory, vk.alloc);
}

} // namespace nyla