#pragma once

#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_texture.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(vk.instance, #name))

namespace nyla::rhi_vulkan_internal
{

using namespace rhi_internal;

inline auto VkCheckImpl(VkResult res)
{
    if (res == VK_ERROR_DEVICE_LOST)
        LOG(ERROR) << "Last checkpoint: " << RhiGetLastCheckpointData(RhiQueueType::Graphics);

    CHECK_EQ(res, VK_SUCCESS) << "Vulkan error: " << string_VkResult(res);
};
#define VK_CHECK(res) VkCheckImpl(res)

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

    RhiFlags flags;

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice physDev;
    VkPhysicalDeviceProperties physDevProps;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    VkDescriptorPool descriptorPool;

    PlatformWindow window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;

    uint32_t swapchainTextureIndex;
    uint32_t swapchainTexturesCount;
    std::array<RhiTexture, kRhiMaxNumSwapchainTextures> swapchainTextures;
    std::array<VkSemaphore, kRhiMaxNumSwapchainTextures> renderFinishedSemaphores;
    std::array<VkSemaphore, kRhiMaxNumFramesInFlight> swapchainAcquireSemaphores;

    DeviceQueue graphicsQueue;
    uint32_t frameIndex;
    uint32_t numFramesInFlight;
    std::array<RhiCmdList, kRhiMaxNumFramesInFlight> graphicsQueueCmd;
    std::array<uint64_t, kRhiMaxNumFramesInFlight> graphicsQueueCmdDone;

    DeviceQueue transferQueue;
    RhiCmdList transferQueueCmd;
    uint64_t transferQueueCmdDone;
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
    bool isSwapchain;
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    VkFormat format;
    VkExtent3D extent;
};

struct RhiHandles
{
    RhiHandlePool<RhiBindGroupLayout, VkDescriptorSetLayout, 16> bindGroupLayouts;
    RhiHandlePool<RhiBindGroup, VkDescriptorSet, 16> bindGroups;
    RhiHandlePool<RhiBuffer, VulkanBufferData, 16> buffers;
    RhiHandlePool<RhiCmdList, VulkanCmdListData, 16> cmdLists;
    RhiHandlePool<RhiShader, VkShaderModule, 16> shaders;
    RhiHandlePool<RhiGraphicsPipeline, VulkanPipelineData, 16> graphicsPipelines;
    RhiHandlePool<RhiTexture, VulkanTextureData, 16> textures;
};
extern RhiHandles rhiHandles;

auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                            const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData) -> VkBool32;

void CreateSwapchain();

auto RhiCreateTextureFromSwapchainImage(VkImage image, VkSurfaceFormatKHR surfaceFormat, VkExtent2D surfaceExtent)
    -> RhiTexture;
void RhiDestroySwapchainTexture(RhiTexture texture);

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