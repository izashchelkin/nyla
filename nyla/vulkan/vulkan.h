#pragma once

#include <cstdint>
#define VK_USE_PLATFORM_XCB_KHR

#include "absl/log/check.h"
#include "vulkan/vulkan.h"

namespace nyla {

struct VkState {
  VkInstance instance;
  VkDevice device;
  VkPhysicalDevice phys_device;
  VkSurfaceKHR surface;
  VkSurfaceCapabilitiesKHR surface_capabilities;
  VkSurfaceFormatKHR surface_format;
  VkExtent2D surface_extent;
  VkPresentModeKHR present_mode;
  VkSwapchainKHR swapchain;
  std::vector<VkImage> swapchain_images;
  std::vector<VkImageView> swapchain_image_views;

  uint32_t swapchain_image_count() { return swapchain_images.size(); }
};
extern VkState vk;

#define VK_CHECK(a) CHECK_EQ(a, VK_SUCCESS);

}  // namespace nyla
