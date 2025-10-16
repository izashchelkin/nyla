#include "nyla/vulkan/swapchain.h"

#include "absl/log/log.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

void CreateSwapchain() {
  VkSwapchainKHR old_swapchain = vk.swapchain;
  std::vector<VkImage> old_images = vk.swapchain_images;
  std::vector<VkImageView> old_image_views = vk.swapchain_image_views;

  {
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        vk.phys_device, vk.surface, &vk.surface_capabilities));
  }

  {
    uint32_t surface_format_count;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        vk.phys_device, vk.surface, &surface_format_count, nullptr));

    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_device, vk.surface,
                                         &surface_format_count,
                                         surface_formats.data());

    auto it = std::find_if(
        surface_formats.begin(), surface_formats.end(),
        [](VkSurfaceFormatKHR surface_format) {
          return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                 surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    CHECK(it != surface_formats.end());

    vk.surface_format = *it;
  }

  {
    std::vector<VkPresentModeKHR> present_modes;
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_device, vk.surface,
                                              &present_mode_count, nullptr);
    CHECK(present_mode_count);

    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        vk.phys_device, vk.surface, &present_mode_count, present_modes.data());

    vk.present_mode = present_modes.front();
  }

  if (vk.surface_capabilities.currentExtent.width ==
      std::numeric_limits<uint32_t>::max()) {
    vk.surface_extent = PlatformGetWindowSize();

    vk.surface_extent.width = std::clamp(
        vk.surface_extent.width, vk.surface_capabilities.minImageExtent.width,
        vk.surface_capabilities.maxImageExtent.width),
    vk.surface_extent.height = std::clamp(
        vk.surface_extent.height, vk.surface_capabilities.minImageExtent.height,
        vk.surface_capabilities.maxImageExtent.height);
  } else {
    vk.surface_extent = vk.surface_capabilities.currentExtent;
  }

  {
    uint32_t min_image_count = vk.surface_capabilities.minImageCount + 1;
    if (vk.surface_capabilities.maxImageCount) {
      min_image_count =
          std::min(vk.surface_capabilities.maxImageCount, min_image_count);
    }

    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = min_image_count,
        .imageFormat = vk.surface_format.format,
        .imageColorSpace = vk.surface_format.colorSpace,
        .imageExtent = vk.surface_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = vk.surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = vk.present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = vk.swapchain,
    };
    VK_CHECK(
        vkCreateSwapchainKHR(vk.device, &create_info, nullptr, &vk.swapchain));
  }

  {
    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swapchain_image_count,
                            nullptr);

    vk.swapchain_images.resize(swapchain_image_count);
    vk.swapchain_image_views.resize(swapchain_image_count);

    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swapchain_image_count,
                            vk.swapchain_images.data());

    for (size_t i = 0; i < swapchain_image_count; ++i) {
      const VkImageViewCreateInfo create_info{
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = vk.swapchain_images[i],
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = vk.surface_format.format,
          .components =
              {
                  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
              },
          .subresourceRange =
              {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
      };

      VK_CHECK(vkCreateImageView(vk.device, &create_info, nullptr,
                                 &vk.swapchain_image_views[i]));
    }
  }

  if (old_swapchain) {
    vkDeviceWaitIdle(vk.device);

    for (const VkImageView image_view : old_image_views)
      vkDestroyImageView(vk.device, image_view, nullptr);

    vkDestroySwapchainKHR(vk.device, old_swapchain, nullptr);
  }
}

}  // namespace nyla
