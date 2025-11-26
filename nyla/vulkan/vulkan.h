#pragma once

#include <cstdint>

#include "absl/log/check.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

namespace nyla {

struct GraphicsFence {
  uint64_t val;
};

struct TransferFence {
  uint64_t val;
};

constexpr static uint32_t kInvalidQueueFamilyIndex = std::numeric_limits<uint32_t>::max();

struct VkQueueState {
  uint32_t family_index;
  VkQueue queue;
  VkSemaphore timeline;
  uint64_t timeline_next;
};

struct VkState {
  uint32_t frames_inflight;

  VkInstance instance;
  VkDevice device;

  VkQueueState graphics_queue;
  std::vector<GraphicsFence> frame_done;
  VkQueueState transfer_queue;
  TransferFence transfer_done;

  VkPhysicalDevice phys_device;
  VkPhysicalDeviceProperties phys_device_props;
  VkSurfaceKHR surface;
  VkSurfaceCapabilitiesKHR surface_capabilities;
  VkSurfaceFormatKHR surface_format;
  VkExtent2D surface_extent;
  VkPresentModeKHR present_mode;
  VkSwapchainKHR swapchain;
  VkCommandPool command_pool;
  std::vector<VkImage> swapchain_images;
  uint32_t swapchain_image_count() {
    return swapchain_images.size();
  }
  std::vector<VkImageView> swapchain_image_views;

  std::vector<VkSemaphore> acquire_semaphores;
  std::vector<VkCommandBuffer> cmd;

  struct {
    uint32_t swapchain_image_index;
    float dt;
    uint8_t iframe;
  } cur;
};
extern VkState vk;

#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(vk.instance, #name))

#define VK_CHECK(res) CHECK_EQ(res, VK_SUCCESS) << "Vulkan error: " << string_VkResult(res);

void Vulkan_Initialize(const char* appname);

void Vulkan_PlatformSetSurface();
VkExtent2D Vulkan_PlatformGetWindowExtent();

//

void VulkanNameHandle(void* object_handle, const std::string& name);

VkPipeline Vulkan_CreateGraphicsPipeline(const VkPipelineVertexInputStateCreateInfo& vertex_input_create_info,
                                         VkPipelineLayout pipeline_layout,
                                         std::span<const VkPipelineShaderStageCreateInfo> stages,
                                         VkPipelineRasterizationStateCreateInfo rasterizer_create_info);

VkShaderModule Vulkan_CreateShaderModule(const std::vector<char>& code);

VkSemaphore VkCreateTimelineSemaphore(uint64_t initial_value);
VkSemaphore VkCreateSemaphore();
VkFence VkCreateFence(bool signaled = false);

//

void Vulkan_FrameBegin();
void Vulkan_RenderingBegin();
void Vulkan_RenderingEnd();
void Vulkan_FrameEnd();

uint32_t GetFps();

//

}  // namespace nyla