#include "nyla/vulkan/vulkan.h"

#define VK_USE_PLATFORM_XCB_KHR

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "vulkan/vulkan.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {}  // namespace nyla

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  int iscreen;
  xcb_connection_t* conn = xcb_connect(nullptr, &iscreen);
  xcb_screen_t* screen = xcb_aux_get_screen(conn, iscreen);

  xcb_window_t window = xcb_generate_id(conn);

  CHECK(!xcb_request_check(
      conn, xcb_create_window_checked(conn, XCB_COPY_FROM_PARENT, window,
                                      screen->root, 0, 0, 100, 100, 0,
                                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                      screen->root_visual, 0, nullptr)));

  xcb_map_window(conn, window);
  xcb_flush(conn);

  //

  auto instance_extensions = std::to_array({
      VK_KHR_XCB_SURFACE_EXTENSION_NAME,
      VK_KHR_SURFACE_EXTENSION_NAME,
  });
  VkInstanceCreateInfo instance_create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .enabledExtensionCount = instance_extensions.size(),
      .ppEnabledExtensionNames = instance_extensions.data(),
  };
  VkInstance instance;
  VkResult res = vkCreateInstance(&instance_create_info, nullptr, &instance);
  CHECK(res == VK_SUCCESS);
  std::cerr << "created instance\n";

  uint32_t num_phys_devices = 1;
  VkPhysicalDevice phys_device;
  res = vkEnumeratePhysicalDevices(instance, &num_phys_devices, &phys_device);
  CHECK(res == VK_SUCCESS || res == VK_INCOMPLETE);

  uint32_t queue_family_property_count = 1;
  VkQueueFamilyProperties queue_family_property;
  vkGetPhysicalDeviceQueueFamilyProperties(
      phys_device, &queue_family_property_count, &queue_family_property);

  CHECK(queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  CHECK(queue_family_property.queueFlags & VK_QUEUE_COMPUTE_BIT);
  CHECK(queue_family_property.queueFlags & VK_QUEUE_TRANSFER_BIT);

  uint32_t queue_family_index = 0;

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queue_family_index,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };

  auto device_extensions = std::to_array({
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  });
  VkDeviceCreateInfo device_create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledExtensionCount = device_extensions.size(),
      .ppEnabledExtensionNames = device_extensions.data(),
  };

  VkDevice device;
  res = vkCreateDevice(phys_device, &device_create_info, nullptr, &device);
  std::cerr << "created device\n";
  CHECK(res == VK_SUCCESS);

  VkQueue queue;
  vkGetDeviceQueue(device, queue_family_index, 0, &queue);

  VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = conn,
      .window = window,
  };

  VkSurfaceKHR surface;
  res =
      vkCreateXcbSurfaceKHR(instance, &surface_create_info, nullptr, &surface);
  CHECK(res == VK_SUCCESS);
  std::cerr << "created surface\n";

  VkSurfaceCapabilitiesKHR surface_capabilities{};
  res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface,
                                                  &surface_capabilities);
  CHECK(res == VK_SUCCESS);

  std::vector<VkSurfaceFormatKHR> surface_formats;
  uint32_t surface_format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface,
                                       &surface_format_count, nullptr);
  CHECK(surface_format_count);

  surface_formats.resize(surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(
      phys_device, surface, &surface_format_count, surface_formats.data());

  auto it = std::find_if(
      surface_formats.begin(), surface_formats.end(),
      [](const auto surface_format) {
        return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
               surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
      });
  CHECK(it != surface_formats.end());

  std::vector<VkPresentModeKHR> present_modes;
  uint32_t present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device, surface,
                                            &present_mode_count, nullptr);
  CHECK(present_mode_count);

  present_modes.resize(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      phys_device, surface, &present_mode_count, present_modes.data());
}
