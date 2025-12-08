#include "absl/log/log.h"
#include <cstdint>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal
{

void CreateSwapchain()
{
    VkSwapchainKHR oldSwapchain = vk.swapchain;

    std::array oldSwapchainTextures = vk.swapchainTextures;
    uint32_t oldImagesViewsCount = vk.swapchainTexturesCount;

    static bool logPresentModes = true;
    VkPresentModeKHR presentMode = [] -> VkPresentModeKHR {
        std::vector<VkPresentModeKHR> presentModes;
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physDev, vk.surface, &presentModeCount, nullptr);

        presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physDev, vk.surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR presentMode : presentModes)
        {
            if (logPresentModes)
                LOG(INFO) << "Present Mode available: " << string_VkPresentModeKHR(presentMode);

            bool better;
            switch (presentMode)
            {

            case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
                better = !Any(vk.flags & RhiFlags::VSync);
                break;
            }
            case VK_PRESENT_MODE_IMMEDIATE_KHR: {
                better = !Any(vk.flags & RhiFlags::VSync) && bestMode != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
                break;
            }

            default: {
                better = false;
                break;
            }
            }

            if (better)
                bestMode = presentMode;
        }

        if (logPresentModes)
            LOG(INFO) << "Chose " << string_VkPresentModeKHR(bestMode);

        logPresentModes = false;

        return bestMode;
    }();

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physDev, vk.surface, &surfaceCapabilities));

    auto surfaceFormat = [] -> VkSurfaceFormatKHR {
        uint32_t surfaceFormatCount;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physDev, vk.surface, &surfaceFormatCount, nullptr));

        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physDev, vk.surface, &surfaceFormatCount, surfaceFormats.data());

        auto it = std::ranges::find_if(surfaceFormats, [](VkSurfaceFormatKHR surfaceFormat) -> bool {
            return surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        CHECK(it != surfaceFormats.end());
        return *it;
    }();

    auto surfaceExtent = [surfaceCapabilities] -> VkExtent2D {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return surfaceCapabilities.currentExtent;

        const PlatformWindowSize windowSize = PlatformGetWindowSize(vk.window);
        return VkExtent2D{
            .width = std::clamp(windowSize.width, surfaceCapabilities.minImageExtent.width,
                                surfaceCapabilities.maxImageExtent.width),
            .height = std::clamp(windowSize.height, surfaceCapabilities.minImageExtent.height,
                                 surfaceCapabilities.maxImageExtent.height),
        };
    }();

    CHECK_GE(kRhiMaxNumSwapchainTextures, surfaceCapabilities.minImageCount);
    uint32_t swapchainMinImageCount = std::min(kRhiMaxNumSwapchainTextures, surfaceCapabilities.minImageCount + 1);
    if (surfaceCapabilities.maxImageCount)
        swapchainMinImageCount = std::min(surfaceCapabilities.maxImageCount, swapchainMinImageCount);

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = swapchainMinImageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = surfaceExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = vk.swapchain,
    };
    VK_CHECK(vkCreateSwapchainKHR(vk.dev, &createInfo, nullptr, &vk.swapchain));

    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &vk.swapchainTexturesCount, nullptr);

    CHECK_LE(vk.swapchainTexturesCount, kRhiMaxNumSwapchainTextures);
    std::array<VkImage, kRhiMaxNumSwapchainTextures> swapchainImages;

    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &vk.swapchainTexturesCount, swapchainImages.data());

    for (size_t i = 0; i < vk.swapchainTexturesCount; ++i)
    {
        vk.swapchainTextures[i] = RhiCreateTextureFromSwapchainImage(swapchainImages[i], surfaceFormat, surfaceExtent);

#if 0
        const VkImageCreateInfo depthImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = VkExtent3D{vk.surfaceExtent.width, vk.surfaceExtent.height, 1},
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
        VK_CHECK(vkCreateImage(vk.dev, &depthImageCreateInfo, vk.alloc, &depthImage));

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
        // VK_CHECK(vkCreateImageView(vk.dev, &depthImageImageViewCreateInfo, vk.alloc, &depthImageView));
#endif
    }

    if (oldSwapchain)
    {
        CHECK_GT(oldImagesViewsCount, 0);
        for (uint32_t i = 0; i < oldImagesViewsCount; ++i)
            RhiDestroySwapchainTexture(oldSwapchainTextures[i]);

        vkDestroySwapchainKHR(vk.dev, oldSwapchain, nullptr);
    }
}

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
        .state = RhiTextureState::Present,
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

auto RhiGetBackbufferTexture() -> RhiTexture
{
    return vk.swapchainTextures[vk.swapchainTextureIndex];
}

} // namespace nyla