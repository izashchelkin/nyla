#include "nyla/vulkan/vulkan.h"

#include <xkbcommon/xkbcommon.h>

#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#define VK_USE_PLATFORM_XCB_KHR

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>

#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "vulkan/vulkan.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {}  // namespace nyla

static std::vector<char> ReadFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  CHECK(file.is_open());

  std::vector<char> buffer(file.tellg());

  file.seekg(0);
  file.read(buffer.data(), buffer.size());

  file.close();
  return buffer;
}

static VkShaderModule CreateShaderModule(VkDevice device,
                                         const std::vector<char>& code) {
  VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };

  VkShaderModule module;
  CHECK(vkCreateShaderModule(device, &create_info, nullptr, &module) ==
        VK_SUCCESS);

  return module;
}

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

  VkSurfaceFormatKHR surface_format = [phys_device, surface]() {
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
        [](VkSurfaceFormatKHR surface_format) {
          return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                 surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    CHECK(it != surface_formats.end());

    return *it;
  }();

  VkPresentModeKHR present_mode = [phys_device, surface]() {
    std::vector<VkPresentModeKHR> present_modes;
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device, surface,
                                              &present_mode_count, nullptr);
    CHECK(present_mode_count);

    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        phys_device, surface, &present_mode_count, present_modes.data());

    return present_modes.front();
  }();

  VkExtent2D extent = [surface_capabilities, conn, window]() {
    if (surface_capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
      return surface_capabilities.currentExtent;
    }

    xcb_get_geometry_reply_t* window_geometry =
        xcb_get_geometry_reply(conn, xcb_get_geometry(conn, window), nullptr);

    return VkExtent2D{
        std::clamp(static_cast<uint32_t>(window_geometry->width),
                   surface_capabilities.minImageExtent.width,
                   surface_capabilities.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(window_geometry->height),
                   surface_capabilities.minImageExtent.height,
                   surface_capabilities.maxImageExtent.height),
    };
  }();

  uint32_t image_count = surface_capabilities.minImageCount + 1;
  if (surface_capabilities.maxImageCount)
    image_count = std::min(surface_capabilities.maxImageCount, image_count);

  VkSwapchainCreateInfoKHR swapchain_create_info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = image_count,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = surface_capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = present_mode,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE,
  };

  VkSwapchainKHR swapchain{};
  CHECK(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr,
                             &swapchain) == VK_SUCCESS);

  uint32_t swapchain_image_count = 0;
  vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);

  std::vector<VkImage> swapchain_images;
  swapchain_images.resize(swapchain_image_count);
  vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count,
                          swapchain_images.data());

  std::vector<VkImageView> swapchain_image_views;
  swapchain_image_views.resize(swapchain_images.size());

  for (size_t i = 0; i < swapchain_images.size(); ++i) {
    VkImageViewCreateInfo image_view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swapchain_images.at(i),
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surface_format.format,
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

    CHECK(vkCreateImageView(device, &image_view_create_info, nullptr,
                            &swapchain_image_views.at(i)) == VK_SUCCESS);
  }

  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages;

  auto vert_shader_module =
      CreateShaderModule(device, ReadFile("nyla/vulkan/shaders/vert.spv"));
  shader_stages.at(0) = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_shader_module,
      .pName = "main",
  };

  auto frag_shader_module =
      CreateShaderModule(device, ReadFile("nyla/vulkan/shaders/frag.spv"));
  shader_stages.at(1) = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader_module,
      .pName = "main",
  };
}
