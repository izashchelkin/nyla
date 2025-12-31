#pragma once

#include <cstdint>

#include "nyla/commons/assert.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_texture.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(this->m_Instance, #name))

namespace nyla
{

namespace rhi_vulkan_internal
{

inline auto VkCheckImpl(VkResult res)
{
    switch (res)
    {
    case VK_SUCCESS:
        break;

    case VK_ERROR_DEVICE_LOST:
        // TODO:
        // NYLA_LOG("Last checkpoint: %I64d", RhiGetLastCheckpointData(RhiQueueType::Graphics));
        // FALLTHROUGH

    default: {
        NYLA_LOG("Vulkan error: %s", string_VkResult(res));
        NYLA_ASSERT(false);
    }
    }
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

struct VulkanBufferData
{
    VkBuffer buffer;
    uint32_t size;
    RhiMemoryUsage memoryUsage;
    VkDeviceMemory memory;
    char *mapped;
    RhiBufferState state;

    uint32_t dirtyBegin;
    uint32_t dirtyEnd;
    bool dirty;
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
    std::array<RhiDescriptorSetLayout, 4> bindGroupLayouts;
    uint32_t bindGroupLayoutCount;
};

struct VulkanTextureData
{
    bool isSwapchain;
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    RhiTextureState state;
    VkImageLayout layout;
    VkFormat format;
    VkExtent3D extent;
};

struct VulkanSamplerData
{
    VkSampler sampler;
};

struct VulkanDescriptorSetLayoutData
{
    VkDescriptorSetLayout layout;
    InlineVec<RhiDescriptorLayoutDesc, 64> descriptors;
};

struct VulkanDescriptorSetData
{
    VkDescriptorSet set;
    RhiDescriptorSetLayout layout;
};

void CreateSwapchain();

} // namespace rhi_vulkan_internal

using namespace rhi_vulkan_internal;

class Rhi::Impl
{
  public:
    auto CreateTimeline(uint64_t initialValue) -> VkSemaphore;
    void WaitTimeline(VkSemaphore timeline, uint64_t waitValue);

    auto FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t;

    void VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name);

    auto ConvertBufferUsageIntoVkBufferUsageFlags(RhiBufferUsage usage) -> VkBufferUsageFlags;

    auto ConvertMemoryUsageIntoVkMemoryPropertyFlags(RhiMemoryUsage usage) -> VkMemoryPropertyFlags;

    auto ConvertVertexFormatIntoVkFormat(RhiVertexFormat format) -> VkFormat;

    auto ConvertTextureFormatIntoVkFormat(RhiTextureFormat format) -> VkFormat;
    auto ConvertVkFormatIntoTextureFormat(VkFormat format) -> RhiTextureFormat;

    auto ConvertTextureUsageToVkImageUsageFlags(RhiTextureUsage usage) -> VkImageUsageFlags;

    auto ConvertShaderStageIntoVkShaderStageFlags(RhiShaderStage stageFlags) -> VkShaderStageFlags;

    auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                VkDebugUtilsMessageTypeFlagsEXT messageType,
                                const VkDebugUtilsMessengerCallbackDataEXT *callbackData) -> VkBool32;

    //

    void Init(const RhiInitDesc &);
    auto GetMinUniformBufferOffsetAlignment() -> uint32_t;
    auto GetOptimalBufferCopyOffsetAlignment() -> uint32_t;

    auto FrameBegin() -> RhiCmdList;
    void FrameEnd();
    auto GetNumFramesInFlight() -> uint32_t;
    auto GetFrameIndex() -> uint32_t;
    auto FrameGetCmdList() -> RhiCmdList;

    auto CreateSampler(const RhiSamplerDesc &desc) -> RhiSampler;
    void DestroySampler(RhiSampler sampler);

    auto CreateBuffer(const RhiBufferDesc &desc) -> RhiBuffer;
    void NameBuffer(RhiBuffer buf, std::string_view name);
    void DestroyBuffer(RhiBuffer buffer);
    auto GetBufferSize(RhiBuffer buffer) -> uint32_t;
    auto MapBuffer(RhiBuffer buffer) -> char *;
    void UnmapBuffer(RhiBuffer buffer);
    void CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState);
    void CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                       uint32_t size);
    void CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer);
    void BufferMarkWritten(RhiBuffer buffer, uint32_t offset, uint32_t size);

    auto CreateTexture(RhiTextureDesc desc) -> RhiTexture;
    auto GetTextureInfo(RhiTexture texture) -> RhiTextureInfo;
    void DestroyTexture(RhiTexture texture);
    void CmdTransitionTexture(RhiCmdList cmd, RhiTexture texture, RhiTextureState newState);
    void CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size);

    void EnsureHostWritesVisible(VkCommandBuffer cmdbuf, VulkanBufferData &bufferData);

    auto CreateCmdList(RhiQueueType queueType) -> RhiCmdList;
    void NameCmdList(RhiCmdList cmd, std::string_view name);
    void DestroyCmdList(RhiCmdList cmd);
    auto CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
    auto GetLastCheckpointData(RhiQueueType queueType) -> uint64_t;
    auto GetDeviceQueue(RhiQueueType queueType) -> DeviceQueue &;

    void PassBegin(RhiPassDesc desc);
    void PassEnd(RhiPassDesc desc);

    auto CreateShader(const RhiShaderDesc &desc) -> RhiShader;
    void DestroyShader(RhiShader shader);

    auto CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &desc) -> RhiGraphicsPipeline;
    void NameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name);
    void DestroyGraphicsPipeline(RhiGraphicsPipeline pipeline);
    void CmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline);
    void CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data);
    void CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                              std::span<const uint32_t> offsets);
    void CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                 uint32_t firstInstance);
    auto GetVertexFormatSize(RhiVertexFormat format) -> uint32_t;

    auto CreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &desc) -> RhiDescriptorSetLayout;
    void DestroyDescriptorSetLayout(RhiDescriptorSetLayout layout);
    auto CreateDescriptorSet(RhiDescriptorSetLayout layout) -> RhiDescriptorSet;
    void WriteDescriptors(std::span<const RhiDescriptorWriteDesc> writes);
    void DestroyDescriptorSet(RhiDescriptorSet bindGroup);
    void CmdBindGraphicsBindGroup(RhiCmdList cmd, uint32_t setIndex, RhiDescriptorSet bindGroup,
                                  std::span<const uint32_t> dynamicOffsets);

    void CreateSwapchain();
    auto CreateTextureFromSwapchainImage(VkImage image, VkSurfaceFormatKHR surfaceFormat, VkExtent2D surfaceExtent)
        -> RhiTexture;
    void DestroySwapchainTexture(RhiTexture texture);
    auto GetBackbufferTexture() -> RhiTexture;

  private:
    HandlePool<RhiDescriptorSetLayout, VulkanDescriptorSetLayoutData, 16> m_DescriptorSetLayouts;
    HandlePool<RhiDescriptorSet, VulkanDescriptorSetData, 16> m_DescriptorSets;
    HandlePool<RhiBuffer, VulkanBufferData, 16> m_Buffers;
    HandlePool<RhiCmdList, VulkanCmdListData, 16> m_CmdLists;
    HandlePool<RhiShader, VkShaderModule, 16> m_Shaders;
    HandlePool<RhiGraphicsPipeline, VulkanPipelineData, 16> m_GraphicsPipelines;
    HandlePool<RhiTexture, VulkanTextureData, 128> m_Textures;
    HandlePool<RhiSampler, VulkanSamplerData, 16> m_Samplers;

    VkAllocationCallbacks *m_Alloc;

    RhiFlags m_Flags;

    VkInstance m_Instance;
    VkDevice m_Dev;
    VkPhysicalDevice m_PhysDev;
    VkPhysicalDeviceProperties m_PhysDevProps;
    VkPhysicalDeviceMemoryProperties m_PhysDevMemProps;
    VkDescriptorPool m_DescriptorPool;

    PlatformWindow m_Window;
    VkSurfaceKHR m_Surface;
    VkSwapchainKHR m_Swapchain;

    uint32_t m_SwapchainTextureIndex;
    uint32_t m_SwapchainTexturesCount;
    std::array<RhiTexture, kRhiMaxNumSwapchainTextures> m_SwapchainTextures;
    std::array<VkSemaphore, kRhiMaxNumSwapchainTextures> m_RenderFinishedSemaphores;
    std::array<VkSemaphore, kRhiMaxNumFramesInFlight> m_SwapchainAcquireSemaphores;

    DeviceQueue m_GraphicsQueue;
    uint32_t m_FrameIndex;
    uint32_t m_NumFramesInFlight;
    std::array<RhiCmdList, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmd;
    std::array<uint64_t, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmdDone;

    DeviceQueue m_TransferQueue;
    RhiCmdList m_TransferQueueCmd;
    uint64_t m_TransferQueueCmdDone;
};

} // namespace nyla