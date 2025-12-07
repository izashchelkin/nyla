#pragma once

#include <cstdint>

#include "absl/log/check.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_texture.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

#define VK_CHECK(res)                                                                                                  \
    CHECK_EQ(res, VK_SUCCESS) << "Vulkan error: " << string_VkResult(res) << "; Last checkpoint "                      \
                              << ((res) == VK_ERROR_DEVICE_LOST ? RhiGetLastCheckpointData(RhiQueueType::Graphics)     \
                                                                : 999999);

#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(vk.instance, #name))

namespace nyla::rhi_vulkan_internal
{

struct DeviceQueue
{
    VkQueue queue;
    uint32_t queueFamilyIndex;
    VkCommandPool cmdPool;

    VkSemaphore timeline;
    uint64_t timelineNext;
};

struct VulkanData
{
    VkAllocationCallbacks *alloc;

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice physDev;
    VkPhysicalDeviceProperties physDevProps;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    VkDescriptorPool descriptorPool;

    PlatformWindow window;
    VkSurfaceKHR surface;
    VkExtent2D surfaceExtent;
    VkSurfaceFormatKHR surfaceFormat;
    VkSwapchainKHR swapchain;
    std::array<VkImage, 8> swapchainImages;
    uint32_t swapchainImageCount;
    std::array<VkImageView, 8> swapchainImageViews;
    std::array<VkSemaphore, kRhiMaxNumFramesInFlight> swapchainAcquireSemaphores;
    std::array<VkSemaphore, 8> renderFinishedSemaphores;

    DeviceQueue graphicsQueue;
    std::array<RhiCmdList, kRhiMaxNumFramesInFlight> graphicsQueueCmd;
    std::array<uint64_t, kRhiMaxNumFramesInFlight> graphicsQueueCmdDone;

    DeviceQueue transferQueue;
    RhiCmdList transferQueueCmd;
    uint64_t transferQueueCmdDone;

    uint32_t numFramesInFlight;
    uint32_t frameIndex;
    uint32_t swapchainImageIndex;
};

extern VulkanData vk;

struct VulkanBufferData
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    char *mapped;
};

struct VulkanCmdListData
{
    VkCommandBuffer cmdbuf;
    RhiQueueType queueType;
    RhiGraphicsPipeline boundGraphicsPipeline;
};

struct VulkanPipelineData
{
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkPipelineBindPoint bindPoint;
    std::array<RhiBindGroupLayout, kRhiMaxBindGroupLayouts> bindGroupLayouts;
    uint32_t bindGroupLayoutCount;
};

struct VulkanTextureData
{
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    VkFormat format;
    VkMemoryPropertyFlags memoryPropertyFlags;
};

struct RhiHandles
{
    rhi_internal::RhiHandlePool<RhiBindGroupLayout, VkDescriptorSetLayout, 16> bindGroupLayouts;
    rhi_internal::RhiHandlePool<RhiBindGroup, VkDescriptorSet, 16> bindGroups;
    rhi_internal::RhiHandlePool<RhiBuffer, VulkanBufferData, 16> buffers;
    rhi_internal::RhiHandlePool<RhiCmdList, VulkanCmdListData, 16> cmdLists;
    rhi_internal::RhiHandlePool<RhiShader, VkShaderModule, 16> shaders;
    rhi_internal::RhiHandlePool<RhiGraphicsPipeline, VulkanPipelineData, 16> graphicsPipelines;
    rhi_internal::RhiHandlePool<RhiTexture, VulkanTextureData, 16> textures;
};
extern RhiHandles rhiHandles;

auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                            const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData) -> VkBool32;

void CreateSwapchain();

auto CreateTimeline(uint64_t initialValue) -> VkSemaphore;
void WaitTimeline(VkSemaphore timeline, uint64_t waitValue);

auto FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t;

void VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name);

auto ConvertRhiBufferUsageIntoVkBufferUsageFlags(RhiBufferUsage usage) -> VkBufferUsageFlags;

auto ConvertRhiMemoryUsageIntoVkMemoryPropertyFlags(RhiMemoryUsage usage) -> VkMemoryPropertyFlags;

auto ConvertRhiVertexFormatIntoVkFormat(RhiVertexFormat format) -> VkFormat;

auto ConvertRhiTextureFormatIntoVkFormat(RhiTextureFormat format) -> VkFormat;
auto ConvertVkFormatIntoRhiTextureFormat(VkFormat format) -> RhiTextureFormat;

auto ConvertRhiShaderStageIntoVkShaderStageFlags(RhiShaderStage stageFlags) -> VkShaderStageFlags;

} // namespace nyla::rhi_vulkan_internal