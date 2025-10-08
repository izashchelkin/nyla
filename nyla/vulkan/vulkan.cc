#include "nyla/vulkan/vulkan.h"

#include <sys/socket.h>

#include <array>
#include <cstdint>
#include <iostream>

#include "absl/log/check.h"
#include "volk/volk.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {}  // namespace nyla

int main() {
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

  VkResult res = volkInitialize();
  CHECK(res == VK_SUCCESS);

  auto instance_extensions = std::to_array({
      VK_KHR_XCB_SURFACE_EXTENSION_NAME,
  });
  VkInstanceCreateInfo instance_create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .enabledExtensionCount = instance_extensions.size(),
      .ppEnabledExtensionNames = instance_extensions.data(),
  };
  VkInstance instance;
  res = vkCreateInstance(&instance_create_info, nullptr, &instance);
  CHECK(res == VK_SUCCESS);

  volkLoadInstance(instance);

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
  CHECK(res == VK_SUCCESS);

  VkQueue queue;
  vkGetDeviceQueue(device, queue_family_index, 0, &queue);

  VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = conn,
      .window = window,
  };

  VkSurfaceKHR_T* surface;
  res =
      vkCreateXcbSurfaceKHR(instance, &surface_create_info, nullptr, &surface);
  CHECK(res == VK_SUCCESS);
}
