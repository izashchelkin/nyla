#pragma once

#define VK_USE_PLATFORM_XCB_KHR

#include <cstdint>

#include "absl/log/check.h"
#include "vulkan/vulkan.h"

namespace nyla {

constexpr uint8_t kVulkan_NumFramesInFlight = 2;

struct Vulkan_State {
  VkInstance instance;
  VkDevice device;
  uint32_t queue_family_index;
  VkQueue queue;
  VkPhysicalDevice phys_device;
  VkSurfaceKHR surface;
  VkSurfaceCapabilitiesKHR surface_capabilities;
  VkSurfaceFormatKHR surface_format;
  VkExtent2D surface_extent;
  VkPresentModeKHR present_mode;
  VkSwapchainKHR swapchain;
  VkCommandPool command_pool;
  std::vector<VkImage> swapchain_images;
  uint32_t swapchain_image_count() { return swapchain_images.size(); }
  std::vector<VkImageView> swapchain_image_views;

  std::vector<VkSemaphore> acquire_semaphores;
  std::vector<VkFence> frame_fences;
  std::vector<VkSemaphore> submit_semaphores;
  std::vector<VkCommandBuffer> command_buffers;

  int shaderdir_inotify_fd;
  bool shaders_recompile = true;
  bool shaders_invalidated = true;

  struct {
    uint32_t swapchain_image_index;
    float dt;
    uint8_t iframe;
  } current_frame_data;
};
extern Vulkan_State vk;

#define VK_GET_INSTANCE_PROC_ADDR(name) \
  reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))

#define VK_CHECK(a) CHECK_EQ(a, VK_SUCCESS);

void Vulkan_Initialize(const char* shader_directory);

void Vulkan_PlatformSetSurface();
VkExtent2D Vulkan_PlatformGetWindowExtent();

//

void Vulkan_CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer,
                         VkDeviceMemory& buffer_memory);

void Vulkan_CreateBuffer(VkCommandPool command_pool, VkQueue transfer_queue,
                         VkDeviceSize data_size, const void* src_data,
                         VkBufferUsageFlags usage, VkBuffer& buffer,
                         VkDeviceMemory& buffer_memory);

//

VkPipeline Vulkan_CreateGraphicsPipeline(
    const VkPipelineVertexInputStateCreateInfo& vertex_input_create_info,
    VkPipelineLayout pipeline_layout,
    std::span<const VkPipelineShaderStageCreateInfo> stages);

VkShaderModule Vulkan_CreateShaderModule(const std::vector<char>& code);

//

inline VkSemaphore CreateSemaphore() {
  const VkSemaphoreCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  VkSemaphore semaphore;
  VK_CHECK(vkCreateSemaphore(vk.device, &create_info, nullptr, &semaphore));
  return semaphore;
}

inline VkFence CreateFence(bool signaled = false) {
  VkFenceCreateFlags flags{};
  if (signaled) flags += VK_FENCE_CREATE_SIGNALED_BIT;

  const VkFenceCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = flags,
  };

  VkFence fence;
  VK_CHECK(vkCreateFence(vk.device, &create_info, nullptr, &fence));
  return fence;
}

//

void Vulkan_FrameBegin();
void Vulkan_RenderingBegin();
void Vulkan_RenderingEnd();
void Vulkan_FrameEnd();

//

void CreateUniformBuffer(VkDescriptorSet descriptor_set, uint32_t dstBinding,
                         bool dynamic, size_t buffer_size, size_t range,
                         VkBuffer& buffer, VkDeviceMemory& memory,
                         void*& mapped);

}  // namespace nyla
