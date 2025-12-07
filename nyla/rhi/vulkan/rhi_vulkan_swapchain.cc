#include "absl/log/log.h"
#include <cstdint>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla::rhi_vulkan_internal
{

void CreateSwapchain()
{
    VkSwapchainKHR oldSwapchain = vk.swapchain;
    auto oldImageViews = vk.swapchainImageViews;
    uint32_t oldImagesViewsCount = vk.swapchainImagesCount;

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physDev, vk.surface, &surfaceCapabilities));

    vk.surfaceFormat = [] -> VkSurfaceFormatKHR {
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

    vk.surfaceExtent = [surfaceCapabilities] -> VkExtent2D {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return surfaceCapabilities.currentExtent;
        }

        const PlatformWindowSize windowSize = PlatformGetWindowSize(vk.window);
        return VkExtent2D{
            .width = std::clamp(windowSize.width, surfaceCapabilities.minImageExtent.width,
                                surfaceCapabilities.maxImageExtent.width),
            .height = std::clamp(windowSize.height, surfaceCapabilities.minImageExtent.height,
                                 surfaceCapabilities.maxImageExtent.height),
        };
    }();

    uint32_t swapchainMinImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount)
    {
        swapchainMinImageCount = std::min(surfaceCapabilities.maxImageCount, swapchainMinImageCount);
    }

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = swapchainMinImageCount,
        .imageFormat = vk.surfaceFormat.format,
        .imageColorSpace = vk.surfaceFormat.colorSpace,
        .imageExtent = vk.surfaceExtent,
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

    uint32_t swapchainImageCount;
    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &swapchainImageCount, nullptr);

    CHECK_LE(swapchainImageCount, std::size(vk.swapchainImages));
    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &swapchainImageCount, vk.swapchainImages.data());

    for (size_t i = 0; i < swapchainImageCount; ++i)
    {
        const VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk.swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk.surfaceFormat.format,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VK_CHECK(vkCreateImageView(vk.dev, &createInfo, nullptr, &vk.swapchainImageViews[i]));
    }

    if (oldSwapchain)
    {
        for (uint32_t i = 0; i < oldImagesViewsCount; ++i)
        {
            vkDestroyImageView(vk.dev, oldImageViews[i], nullptr);
        }

        vkDestroySwapchainKHR(vk.dev, oldSwapchain, nullptr);
    }
}

auto RhiGetBackbufferFormat() -> RhiTextureFormat
{
    return ConvertVkFormatIntoRhiTextureFormat(vk.surfaceFormat.format);
}

} // namespace nyla::rhi_vulkan_internal