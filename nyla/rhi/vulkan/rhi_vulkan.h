#pragma once

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

struct VulkanData {
  VkInstance instance;
  VkDevice dev;
  VkPhysicalDevice phys_dev;
  VkPhysicalDeviceProperties phys_dev_props;
  VkPhysicalDeviceMemoryProperties phys_dev_mem_props;
  VkDescriptorPool descriptor_pool;

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
  RhiCmdList graphics_queue_cmd[rhi_max_num_frames_in_flight];
  uint64_t graphics_queue_cmd_done[rhi_max_num_frames_in_flight];

  DeviceQueue transfer_queue;
  RhiCmdList transfer_queue_cmd;
  uint64_t transfer_queue_cmd_done;

  uint32_t num_frames_in_flight;
  uint32_t frame_index;
  uint32_t swapchain_image_index;
};

extern VulkanData vk;

struct VulkanBufferData {
  VkBuffer buffer;
  VkDeviceMemory memory;
  char* mapped;
};

struct VulkanCmdListData {
  VkCommandBuffer command_buffer;
  RhiQueueType queue_type;
  RhiGraphicsPipeline bound_graphics_pipeline;
};

struct VulkanPipelineData {
  VkPipelineLayout layout;
  VkPipeline pipeline;
  VkPipelineBindPoint bind_point;
  RhiBindGroupLayout bind_group_layouts[rhi_max_bind_group_layouts];
  uint32_t bind_group_layout_count;
};

#define NYLA_RHI_HANDLE_POOLS(X)                                                                   \
  X rhi_internal::RhiHandlePool<RhiBindGroupLayout, VkDescriptorSetLayout, 16> bind_group_layouts; \
  X rhi_internal::RhiHandlePool<RhiBindGroup, VkDescriptorSet, 16> bind_groups;                    \
  X rhi_internal::RhiHandlePool<RhiBuffer, VulkanBufferData, 16> buffers;                          \
  X rhi_internal::RhiHandlePool<RhiCmdList, VulkanCmdListData, 16> cmd_lists;                      \
  X rhi_internal::RhiHandlePool<RhiShader, VkShaderModule, 16> shaders;                            \
  X rhi_internal::RhiHandlePool<RhiGraphicsPipeline, VulkanPipelineData, 16> graphics_pipelines;

NYLA_RHI_HANDLE_POOLS(extern)

VkBool32 DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                VkDebugUtilsMessageTypeFlagsEXT message_type,
                                const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data);

void CreateSwapchain();

VkSemaphore CreateTimeline(uint64_t initial_value);
void WaitTimeline(VkSemaphore timeline, uint64_t wait_value);

VkFormat ConvertVulkanFormat(RhiFormat format);

}  // namespace rhi_vulkan_internal

}  // namespace nyla