#include "absl/log/check.h"
#include "nyla/rhi/rhi.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

#define VK_CHECK(res) CHECK_EQ(res, VK_SUCCESS) << "Vulkan error: " << string_VkResult(res);
#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(vk.instance, #name))

namespace nyla {

namespace rhi_vulkan_internal {

struct DeviceQueue {
  VkQueue queue;
  uint32_t queue_family_index;
  VkCommandPool cmd_pool;

  VkSemaphore timeline;
  uint64_t timeline_next;
};

static struct {
  VkInstance instance;
  VkDevice dev;
  VkPhysicalDevice phys_dev;
  VkPhysicalDeviceProperties phys_dev_props;
  VkPhysicalDeviceMemoryProperties phys_dev_mem_props;

  PlatformWindow window;
  VkSurfaceKHR surface;
  VkExtent2D surface_extent;
  VkSurfaceFormatKHR surface_format;
  VkSwapchainKHR swapchain;
  VkImage swapchain_images[8];
  uint32_t swapchain_image_count;
  VkImageView swapchain_image_views[8];
  VkSemaphore swapchain_acquire_semaphores[8];

  DeviceQueue graphics_queue;
  VkCommandBuffer graphics_queue_cmd[rhi_max_num_frames_in_flight];
  uint64_t graphics_queue_cmd_done[rhi_max_num_frames_in_flight];

  DeviceQueue transfer_queue;
  VkCommandBuffer transfer_queue_cmd;
  uint64_t transfer_queue_cmd_done;

  uint32_t num_frames_in_flight;
  uint32_t frame_index;
  uint32_t swapchain_image_index;
} vk;

VkBool32 DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                VkDebugUtilsMessageTypeFlagsEXT message_type,
                                const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data);

void CreateSwapchain();

VkSemaphore CreateTimeline(uint64_t initial_value);
void WaitTimeline(VkSemaphore timeline, uint64_t wait_value);

VkFormat ConvertVulkanFormat(RhiFormat format);

}  // namespace rhi_vulkan_internal

}  // namespace nyla