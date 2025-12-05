#include <cstdint>

#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla::rhi_vulkan_internal
{

void CreateSwapchain()
{
    VkSwapchainKHR old_swapchain = vk.swapchain;
    auto old_image_views = vk.swapchain_image_views;
    uint32_t old_images_views_count = vk.swapchain_image_count;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys_dev, vk.surface, &surface_capabilities));

    vk.surface_format = [] -> VkSurfaceFormatKHR {
        uint32_t surface_format_count;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_dev, vk.surface, &surface_format_count, nullptr));

        std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_dev, vk.surface, &surface_format_count, surface_formats.data());

        auto it = std::ranges::find_if(surface_formats, [](VkSurfaceFormatKHR surface_format) -> bool {
            return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        CHECK(it != surface_formats.end());
        return *it;
    }();

    VkPresentModeKHR present_mode = [] -> VkPresentModeKHR {
        std::vector<VkPresentModeKHR> present_modes;
        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_dev, vk.surface, &present_mode_count, nullptr);
        CHECK(present_mode_count);

        present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_dev, vk.surface, &present_mode_count, present_modes.data());

        return VK_PRESENT_MODE_FIFO_KHR; // TODO:
    }();

    vk.surface_extent = [surface_capabilities] -> VkExtent2D {
        if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return surface_capabilities.currentExtent;
        }

        const PlatformWindowSize window_size = PlatformGetWindowSize(vk.window);
        return VkExtent2D{
            .width = std::clamp(window_size.width, surface_capabilities.minImageExtent.width,
                                surface_capabilities.maxImageExtent.width),
            .height = std::clamp(window_size.height, surface_capabilities.minImageExtent.height,
                                 surface_capabilities.maxImageExtent.height),
        };
    }();

    uint32_t swapchain_min_image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount)
    {
        swapchain_min_image_count = std::min(surface_capabilities.maxImageCount, swapchain_min_image_count);
    }

    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = swapchain_min_image_count,
        .imageFormat = vk.surface_format.format,
        .imageColorSpace = vk.surface_format.colorSpace,
        .imageExtent = vk.surface_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = vk.swapchain,
    };
    VK_CHECK(vkCreateSwapchainKHR(vk.dev, &create_info, nullptr, &vk.swapchain));

    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &swapchain_image_count, nullptr);

    CHECK_LE(swapchain_image_count, std::size(vk.swapchain_images));
    vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &swapchain_image_count, vk.swapchain_images.data());

    for (size_t i = 0; i < swapchain_image_count; ++i)
    {
        const VkImageViewCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk.swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk.surface_format.format,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VK_CHECK(vkCreateImageView(vk.dev, &create_info, nullptr, &vk.swapchain_image_views[i]));
    }

    if (old_swapchain)
    {
        for (uint32_t i = 0; i < old_images_views_count; ++i)
        {
            vkDestroyImageView(vk.dev, old_image_views[i], nullptr);
        }

        vkDestroySwapchainKHR(vk.dev, old_swapchain, nullptr);
    }
}

auto RhiGetBackbufferFormat() -> RhiTextureFormat
{
    return ConvertVkFormatIntoRhiTextureFormat(vk.surface_format.format);
}

} // namespace nyla::rhi_vulkan_internal