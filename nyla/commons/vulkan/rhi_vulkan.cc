#include "nyla/commons/vulkan/rhi_vulkan.h"

#include <cstdint>

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "nyla/commons/array.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/collections.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/log.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span.h"
#include "nyla/spirview/spirview.h"

#define spv_enable_utility_code

// clang-format off
#if defined(__linux__)
#include "nyla/commons/linux/platform_linux.h"
#include "vulkan/vulkan_xcb.h"

#include <spirv/unified1/spirv.hpp>
#else
#include "nyla/commons/windows/platform_windows.h"
#include "vulkan/vulkan_win32.h"

#include <spirv-headers/spirv.hpp>
#endif
// clang-format on

#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(g_State->m_Instance, #name))
#define VK_CHECK(res) VkCheckImpl(res)

namespace nyla
{

namespace
{

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
    uint64_t size;
    RhiMemoryUsage memoryUsage;
    VkDeviceMemory memory;
    char *mapped;
    RhiBufferState state;

    uint32_t dirtyBegin;
    uint32_t dirtyEnd;
    bool dirty;
};

struct VulkanBufferViewData
{
    RhiBuffer buffer;
    uint32_t size;
    uint32_t offset;
    uint32_t range;
    bool dynamic;

    bool descriptorWritten;
};

struct VulkanCmdListData
{
    VkCommandBuffer cmdbuf;
    RhiQueueType queueType;
    RhiGraphicsPipeline boundGraphicsPipeline;

    uint32_t frameConstantHead;
    uint32_t passConstantHead;
    uint32_t drawConstantHead;
    uint32_t largeDrawConstantHead;
};

struct VulkanPipelineData
{
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkPipelineBindPoint bindPoint;
};

struct VulkanShaderData
{
    Span<uint32_t> spv;
};

struct VulkanTextureData
{
    VkImage image;
    VkDeviceMemory memory;
    RhiTextureState state;
    VkImageLayout layout;
    VkFormat format;
    VkImageAspectFlags aspectMask;
    VkExtent3D extent;
};

struct VulkanTextureViewData
{
    RhiTexture texture;

    VkImageView imageView;
    VkImageViewType imageViewType;
    VkFormat format;
    VkImageSubresourceRange subresourceRange;

    bool descriptorWritten;
};

struct VulkanSamplerData
{
    VkSampler sampler;

    bool descriptorWritten;
};

auto ConvertBufferUsageIntoVkBufferUsageFlags(RhiBufferUsage usage) -> VkBufferUsageFlags
{
    VkBufferUsageFlags ret = 0;

    if (Any(usage & RhiBufferUsage::Vertex))
    {
        ret |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (Any(usage & RhiBufferUsage::Index))
    {
        ret |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (Any(usage & RhiBufferUsage::Uniform))
    {
        ret |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (Any(usage & RhiBufferUsage::CopySrc))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & RhiBufferUsage::CopyDst))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return ret;
}

auto ConvertMemoryUsageIntoVkMemoryPropertyFlags(RhiMemoryUsage usage) -> VkMemoryPropertyFlags
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    switch (usage)
    {
    case RhiMemoryUsage::Unknown:
        break;
    case RhiMemoryUsage::GpuOnly:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case RhiMemoryUsage::CpuToGpu:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case RhiMemoryUsage::GpuToCpu:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
               VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    NYLA_ASSERT(false);
    return 0;
}

auto ConvertVertexFormatIntoVkFormat(RhiVertexFormat format) -> VkFormat
{
    switch (format)
    {
    case RhiVertexFormat::None:
        break;
    case RhiVertexFormat::R32G32B32A32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RhiVertexFormat::R32G32B32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case RhiVertexFormat::R32G32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    }
    NYLA_ASSERT(false);
    return static_cast<VkFormat>(0);
}

auto ConvertShaderStageIntoVkShaderStageFlags(RhiShaderStage stageFlags) -> VkShaderStageFlags
{
    VkShaderStageFlags ret = 0;
    if (Any(stageFlags & RhiShaderStage::Vertex))
    {
        ret |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (Any(stageFlags & RhiShaderStage::Pixel))
    {
        ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return ret;
}

auto ConvertVulkanCullMode(RhiCullMode cullMode) -> VkCullModeFlags
{
    switch (cullMode)
    {
    case RhiCullMode::None:
        return VK_CULL_MODE_NONE;

    case RhiCullMode::Back:
        return VK_CULL_MODE_BACK_BIT;

    case RhiCullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    }
    NYLA_ASSERT(false);
    return static_cast<VkCullModeFlags>(0);
}

auto ConvertVulkanFrontFace(RhiFrontFace frontFace) -> VkFrontFace
{
    switch (frontFace)
    {
    case RhiFrontFace::CCW:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;

    case RhiFrontFace::CW:
        return VK_FRONT_FACE_CLOCKWISE;
    }
    NYLA_ASSERT(false);
    return static_cast<VkFrontFace>(0);
}

auto ConvertFilter(RhiFilter filter) -> VkFilter
{
    switch (filter)
    {
    case RhiFilter::Linear:
        return VK_FILTER_LINEAR;
    case RhiFilter::Nearest:
        return VK_FILTER_NEAREST;
    }
    NYLA_ASSERT(false);
    return {};
}

auto ConvertSamplerAddressMode(RhiSamplerAddressMode addressMode) -> VkSamplerAddressMode
{
    switch (addressMode)
    {
    case RhiSamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RhiSamplerAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    NYLA_ASSERT(false);
    return {};
}

auto ConvertVulkanInputRate(RhiInputRate inputRate) -> VkVertexInputRate
{
    switch (inputRate)
    {
    case RhiInputRate::PerInstance:
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    case RhiInputRate::PerVertex:
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }
    NYLA_ASSERT(false);
    return static_cast<VkVertexInputRate>(0);
}

struct VulkanTextureStateSyncInfo
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

auto VulkanTextureStateGetSyncInfo(RhiTextureState state) -> VulkanTextureStateSyncInfo
{
    switch (state)
    {
    case RhiTextureState::Undefined:
        return {
            .stage = VK_PIPELINE_STAGE_2_NONE,
            .access = VK_ACCESS_2_NONE,
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

    case RhiTextureState::ColorTarget:
        return {
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

    case RhiTextureState::DepthTarget:
        return {
            .stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

    case RhiTextureState::ShaderRead:
        return {
            .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

    case RhiTextureState::Present:
        return {
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access = VK_ACCESS_2_NONE,
            .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

    case RhiTextureState::TransferSrc:
        return {
            .stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .access = VK_ACCESS_2_TRANSFER_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        };

    case RhiTextureState::TransferDst:
        return {
            .stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        };
    }
    NYLA_ASSERT(false);
    return {};
}

auto ConvertTextureFormatIntoVkFormat(RhiTextureFormat format, VkImageAspectFlags *outAspectMask) -> VkFormat
{
    switch (format)
    {
    case RhiTextureFormat::None: {
        if (outAspectMask)
            *outAspectMask = 0;
        return VK_FORMAT_UNDEFINED;
    }
    case RhiTextureFormat::R8G8B8A8_sRGB: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        return VK_FORMAT_R8G8B8A8_SRGB;
    }
    case RhiTextureFormat::B8G8R8A8_sRGB: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        return VK_FORMAT_B8G8R8A8_SRGB;
    }
    case RhiTextureFormat::D32_Float: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        return VK_FORMAT_D32_SFLOAT;
    }
    case RhiTextureFormat::D32_Float_S8_UINT: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    }
    NYLA_ASSERT(false);
    return static_cast<VkFormat>(0);
}

auto ConvertVkFormatIntoTextureFormat(VkFormat format) -> RhiTextureFormat
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_SRGB:
        return RhiTextureFormat::R8G8B8A8_sRGB;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return RhiTextureFormat::B8G8R8A8_sRGB;
    case VK_FORMAT_D32_SFLOAT:
        return RhiTextureFormat::D32_Float;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return RhiTextureFormat::D32_Float_S8_UINT;
    default:
        break;
    }
    NYLA_ASSERT(false);
    return static_cast<RhiTextureFormat>(0);
}

auto ConvertTextureUsageToVkImageUsageFlags(RhiTextureUsage usage) -> VkImageUsageFlags
{
    VkImageUsageFlags flags = 0;

    if (Any(usage & RhiTextureUsage::ShaderSampled))
    {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (Any(usage & RhiTextureUsage::ColorTarget))
    {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (Any(usage & RhiTextureUsage::DepthStencil))
    {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (Any(usage & RhiTextureUsage::TransferSrc))
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & RhiTextureUsage::TransferDst))
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (Any(usage & RhiTextureUsage::Storage))
    {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (Any(usage & RhiTextureUsage::Present))
    {
    }

    return flags;
}

auto GetVertexFormatSize(RhiVertexFormat format) -> uint32_t
{
    switch (format)
    {
    case RhiVertexFormat::None:
        break;
    case RhiVertexFormat::R32G32Float:
        return 8;
    case RhiVertexFormat::R32G32B32Float:
        return 12;
    case RhiVertexFormat::R32G32B32A32Float:
        return 16;
    }
    NYLA_ASSERT(false);
    return 0;
}

auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                            const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *pUserData) -> VkBool32
{
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
        NYLA_LOG("%s", callbackData->pMessage);
        NYLA_DEBUGBREAK();
        break;
    }
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
        NYLA_LOG("%s", callbackData->pMessage);
        break;
    }
    default: {
        NYLA_LOG("%s", callbackData->pMessage);
        break;
    }
    }
    return VK_FALSE;
}

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

//

struct DescriptorTable
{
    VkDescriptorSetLayout layout;
    VkDescriptorSet set;
};

struct RhiGlobalState
{
    RegionAlloc rootAlloc;
    RegionAlloc transientAlloc;
    RegionAlloc permanentAlloc;

    HandlePool<RhiCmdList, VulkanCmdListData, 16> m_CmdLists;

    HandlePool<RhiShader, VulkanShaderData, 16> m_Shaders;
    HandlePool<RhiGraphicsPipeline, VulkanPipelineData, 16> m_GraphicsPipelines;

    HandlePool<RhiBuffer, VulkanBufferData, 16> m_Buffers;
    HandlePool<RhiBuffer, VulkanBufferViewData, 16> m_CBVs;

    HandlePool<RhiTexture, VulkanTextureData, 128> m_Textures;
    HandlePool<RhiRenderTargetView, VulkanTextureViewData, 8> m_RenderTargetViews;
    HandlePool<RhiDepthStencilView, VulkanTextureViewData, 8> m_DepthStencilViews;
    HandlePool<RhiSampledTextureView, VulkanTextureViewData, 128> m_SampledTextureViews;

    HandlePool<RhiSampler, VulkanSamplerData, 16> m_Samplers;

    VkAllocationCallbacks *vkAlloc;

    RhiFlags m_Flags;
    RhiLimits m_Limits;

    VkInstance m_Instance;
    VkDevice m_Dev;
    VkPhysicalDevice m_PhysDev;
    VkPhysicalDeviceProperties m_PhysDevProps;
    VkPhysicalDeviceMemoryProperties m_PhysDevMemProps;
    VkDescriptorPool m_DescriptorPool;

    DescriptorTable m_ConstantsDescriptorTable;
    RhiBuffer m_ConstantsUniformBuffer;
    DescriptorTable m_TexturesDescriptorTable;
    DescriptorTable m_SamplersDescriptorTable;

    VkSurfaceKHR m_Surface;
    VkSwapchainKHR m_Swapchain;
    bool m_SwapchainUsable = true;
    bool m_RecreateSwapchain = true;

    InlineVec<RhiRenderTargetView, kRhiMaxNumSwapchainTextures> m_SwapchainRTVs{};
    uint32_t m_SwapchainTextureIndex;

    Array<VkSemaphore, kRhiMaxNumSwapchainTextures> m_RenderFinishedSemaphores;
    Array<VkSemaphore, kRhiMaxNumFramesInFlight> m_SwapchainAcquireSemaphores;

    DeviceQueue m_GraphicsQueue;
    uint32_t m_FrameIndex;
    Array<RhiCmdList, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmd;
    Array<uint64_t, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmdDone;

    DeviceQueue m_TransferQueue;
    RhiCmdList m_TransferQueueCmd;
    uint64_t m_TransferQueueCmdDone;
};
RhiGlobalState *g_State;

//

void VulkanNameHandle(VkObjectType type, uint64_t handle, Str name)
{
    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name.Data(),
    };

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkSetDebugUtilsObjectNameEXT);
    fn(g_State->m_Dev, &nameInfo);
}

auto CbvOffset(uint32_t offset) -> uint32_t
{
    return AlignedUp<uint32_t>(offset, g_State->m_PhysDevProps.limits.minUniformBufferOffsetAlignment);
}

auto FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    static const VkPhysicalDeviceMemoryProperties memPropertities = [] -> VkPhysicalDeviceMemoryProperties {
        VkPhysicalDeviceMemoryProperties memPropertities;
        vkGetPhysicalDeviceMemoryProperties(g_State->m_PhysDev, &memPropertities);
        return memPropertities;
    }();

    for (uint32_t i = 0; i < memPropertities.memoryTypeCount; ++i)
    {
        if (!(memRequirements.memoryTypeBits & (1 << i)))
        {
            continue;
        }

        if ((memPropertities.memoryTypes[i].propertyFlags & properties) != properties)
        {
            continue;
        }

        return i;
    }

    NYLA_ASSERT(false);
    return 0;
}

void EnsureHostWritesVisible(VkCommandBuffer cmdbuf, VulkanBufferData &bufferData)
{
    if (bufferData.memoryUsage != RhiMemoryUsage::CpuToGpu)
        return;
    if (!bufferData.dirty)
        return;

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = bufferData.buffer,
        .offset = bufferData.dirtyBegin,
        .size = bufferData.dirtyEnd - bufferData.dirtyBegin,
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    bufferData.dirty = false;
}

struct VulkanBufferStateSyncInfo
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

auto VulkanBufferStateGetSyncInfo(RhiBufferState state) -> VulkanBufferStateSyncInfo
{
    switch (state)
    {
    case RhiBufferState::Undefined: {
        return {.stage = VK_PIPELINE_STAGE_2_NONE, .access = VK_ACCESS_2_NONE};
    }
    case RhiBufferState::CopySrc: {
        return {.stage = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_READ_BIT};
    }
    case RhiBufferState::CopyDst: {
        return {.stage = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_WRITE_BIT};
    }
    case RhiBufferState::ShaderRead: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    }
    case RhiBufferState::ShaderWrite: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
    }
    case RhiBufferState::Vertex: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    }
    case RhiBufferState::Index: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, .access = VK_ACCESS_2_INDEX_READ_BIT};
    }
    case RhiBufferState::Indirect: {
        return {.stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT};
    }
    }
    NYLA_ASSERT(false);
    return {};
}

auto GetDeviceQueue(RhiQueueType queueType) -> DeviceQueue &
{
    switch (queueType)
    {
    case RhiQueueType::Graphics:
        return g_State->m_GraphicsQueue;
    case RhiQueueType::Transfer:
        return g_State->m_TransferQueue;
    }
    NYLA_ASSERT(false);
    return g_State->m_GraphicsQueue;
}

void CmdDrawInternal(VulkanCmdListData &cmdData)
{
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    const VulkanPipelineData &pipelineData = g_State->m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    const Array<uint32_t, 4> offsets{
        cmdData.frameConstantHead,
        cmdData.passConstantHead,
        cmdData.drawConstantHead,
        cmdData.largeDrawConstantHead,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 0, 1,
                            &g_State->m_ConstantsDescriptorTable.set, offsets.Size(), offsets.Data());

    cmdData.drawConstantHead += CbvOffset(g_State->m_Limits.drawConstantSize);
    cmdData.largeDrawConstantHead += CbvOffset(g_State->m_Limits.largeDrawConstantSize);
}

void CreateSwapchain()
{
    VkSwapchainKHR oldSwapchain = g_State->m_Swapchain;

    static bool logPresentModes = true;
    VkPresentModeKHR presentMode = [] -> VkPresentModeKHR {
        auto &presentModes = g_State->transientAlloc.PushVec<VkPresentModeKHR, 16>();
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_State->m_PhysDev, g_State->m_Surface, &presentModeCount, nullptr);

        presentModes.Resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_State->m_PhysDev, g_State->m_Surface, &presentModeCount,
                                                  presentModes.Data());

        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR presentMode : presentModes)
        {
            if (logPresentModes)
                NYLA_LOG("Present Mode available: %s", string_VkPresentModeKHR(presentMode));

            bool better;
            switch (presentMode)
            {

            case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
                better = !Any(g_State->m_Flags & RhiFlags::VSync);
                break;
            }
            case VK_PRESENT_MODE_IMMEDIATE_KHR: {
                better = !Any(g_State->m_Flags & RhiFlags::VSync) && bestMode != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
                break;
            }

            default: {
                better = false;
                break;
            }
            }

            if (better)
                bestMode = presentMode;
        }

        if (logPresentModes)
        {
            NYLA_LOG("Chose %s", string_VkPresentModeKHR(bestMode));
        }

        logPresentModes = false;

        return bestMode;
    }();

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_State->m_PhysDev, g_State->m_Surface, &surfaceCapabilities));

    auto surfaceFormat = [] -> VkSurfaceFormatKHR {
        uint32_t surfaceFormatCount;
        VK_CHECK(
            vkGetPhysicalDeviceSurfaceFormatsKHR(g_State->m_PhysDev, g_State->m_Surface, &surfaceFormatCount, nullptr));

        auto &surfaceFormats = g_State->transientAlloc.PushVec<VkSurfaceFormatKHR, 16>();
        surfaceFormats.Resize(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_State->m_PhysDev, g_State->m_Surface, &surfaceFormatCount,
                                             surfaceFormats.Data());

        auto it = std::ranges::find_if(surfaceFormats, [](VkSurfaceFormatKHR surfaceFormat) -> bool {
            return surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        NYLA_ASSERT(it != surfaceFormats.end());
        return *it;
    }();

    auto surfaceExtent = [surfaceCapabilities] -> VkExtent2D {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return surfaceCapabilities.currentExtent;

        const PlatformWindowSize windowSize = Platform::WinGetSize();
        return VkExtent2D{
            .width = Clamp(windowSize.width, surfaceCapabilities.minImageExtent.width,
                           surfaceCapabilities.maxImageExtent.width),
            .height = Clamp(windowSize.height, surfaceCapabilities.minImageExtent.height,
                            surfaceCapabilities.maxImageExtent.height),
        };
    }();

    NYLA_ASSERT(kRhiMaxNumSwapchainTextures >= surfaceCapabilities.minImageCount);
    uint32_t swapchainMinImageCount = std::min(kRhiMaxNumSwapchainTextures, surfaceCapabilities.minImageCount + 1);
    if (surfaceCapabilities.maxImageCount)
        swapchainMinImageCount = std::min(surfaceCapabilities.maxImageCount, swapchainMinImageCount);

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_State->m_Surface,
        .minImageCount = swapchainMinImageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = surfaceExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = g_State->m_Swapchain,
    };
    VK_CHECK(vkCreateSwapchainKHR(g_State->m_Dev, &createInfo, nullptr, &g_State->m_Swapchain));

    uint32_t swapchainTexturesCount;
    vkGetSwapchainImagesKHR(g_State->m_Dev, g_State->m_Swapchain, &swapchainTexturesCount, nullptr);

    NYLA_ASSERT(swapchainTexturesCount <= kRhiMaxNumSwapchainTextures);
    Array<VkImage, kRhiMaxNumSwapchainTextures> swapchainImages;

    vkGetSwapchainImagesKHR(g_State->m_Dev, g_State->m_Swapchain, &swapchainTexturesCount, swapchainImages.Data());

    for (size_t i = 0; i < swapchainMinImageCount; ++i)
    {
        RhiTexture texture;
        RhiRenderTargetView rtv;

        if (g_State->m_SwapchainRTVs.Size() > i)
        {
            rtv = g_State->m_SwapchainRTVs[i];
            VulkanTextureViewData &rtvData = g_State->m_RenderTargetViews.ResolveData(rtv);
            rtvData.format = surfaceFormat.format;

            texture = rtvData.texture;
            VulkanTextureData &textureData = g_State->m_Textures.ResolveData(texture);
            textureData.image = swapchainImages[i];
            textureData.state = RhiTextureState::Present;
            textureData.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            textureData.format = surfaceFormat.format;
            textureData.extent = VkExtent3D{surfaceExtent.width, surfaceExtent.height, 1};
            textureData.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vkDestroyImageView(g_State->m_Dev, rtvData.imageView, g_State->vkAlloc);

            const VkImageViewCreateInfo imageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = textureData.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = rtvData.format,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCreateImageView(g_State->m_Dev, &imageViewCreateInfo, g_State->vkAlloc, &rtvData.imageView);
        }
        else
        {
            const VulkanTextureData textureData{
                .image = swapchainImages[i],
                .memory = nullptr,
                .state = RhiTextureState::Present,
                .format = surfaceFormat.format,
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .extent = VkExtent3D{surfaceExtent.width, surfaceExtent.height, 1},
            };

            texture = g_State->m_Textures.Acquire(textureData);

            rtv = Rhi::CreateRenderTargetView(RhiRenderTargetViewDesc{
                .texture = texture,
            });

            g_State->m_SwapchainRTVs.PushBack(rtv);
        }
    }

    if (oldSwapchain)
        vkDestroySwapchainKHR(g_State->m_Dev, oldSwapchain, g_State->vkAlloc);
}

auto CreateTimeline(uint64_t initialValue) -> VkSemaphore
{
    const VkSemaphoreTypeCreateInfo timelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = initialValue,
    };

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timelineCreateInfo,
    };

    VkSemaphore semaphore;
    vkCreateSemaphore(g_State->m_Dev, &semaphoreCreateInfo, nullptr, &semaphore);
    return semaphore;
}

void WaitTimeline(VkSemaphore timeline, uint64_t waitValue)
{
    const VkSemaphoreWaitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext = NULL,
        .flags = 0,
        .semaphoreCount = 1,
        .pSemaphores = &timeline,
        .pValues = &waitValue,
    };

    VK_CHECK(vkWaitSemaphores(g_State->m_Dev, &waitInfo, 1e9));

#if 0
    {
        uint64_t currentValue = -1;
        vkGetSemaphoreCounterValue(g_State->m_Dev, timeline, &currentValue);
        DebugBreak();

        VkSemaphoreSignalInfo info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = timeline,
            .value = waitValue,
        };
        VK_CHECK(vkSignalSemaphore(g_State->m_Dev, &info));

        vkGetSemaphoreCounterValue(g_State->m_Dev, g_State->m_GraphicsQueue.timeline, &currentValue);
        DebugBreak();
    }
#endif
}

void WriteDescriptorTables()
{
    g_State->transientAlloc.VoidScope(1_KiB, [](auto &alloc) -> void {
        constexpr uint32_t kMaxDescriptorUpdates = 128;

        auto &descriptorWrites = alloc.template PushVec<VkWriteDescriptorSet, kMaxDescriptorUpdates>();
        auto &descriptorImageInfos = alloc.template PushVec<VkDescriptorImageInfo, kMaxDescriptorUpdates>();
        auto &descriptorBufferInfos = alloc.template PushVec<VkDescriptorBufferInfo, kMaxDescriptorUpdates>();

        { // TEXTURES
            for (uint32_t i = 0; i < g_State->m_SampledTextureViews.Size(); ++i)
            {
                auto &slot = g_State->m_SampledTextureViews[i];
                if (!slot.used)
                    continue;
                if (slot.data.descriptorWritten)
                    continue;

                const VulkanTextureViewData &textureViewData = slot.data;
                const VulkanTextureData &textureData = g_State->m_Textures.ResolveData(textureViewData.texture);

                VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.PushBack(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = g_State->m_TexturesDescriptorTable.set,
                    .dstBinding = 0,
                    .dstArrayElement = i,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                });

                vulkanSetWrite.pImageInfo = &descriptorImageInfos.PushBack(VkDescriptorImageInfo{
                    .imageView = textureViewData.imageView,
                    .imageLayout = textureData.layout,
                });

                slot.data.descriptorWritten = true;
            }
        }

        { // SAMPLERS
            for (uint32_t i = 0; i < g_State->m_Samplers.Size(); ++i)
            {
                auto &slot = g_State->m_Samplers[i];
                if (!slot.used)
                    continue;
                if (slot.data.descriptorWritten)
                    continue;

                const VulkanSamplerData &samplerData = slot.data;

                VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.PushBack(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = g_State->m_SamplersDescriptorTable.set,
                    .dstBinding = 0,
                    .dstArrayElement = i,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                });

                vulkanSetWrite.pImageInfo = &descriptorImageInfos.PushBack(VkDescriptorImageInfo{
                    .sampler = samplerData.sampler,
                });

                slot.data.descriptorWritten = true;
            }
        }

        vkUpdateDescriptorSets(g_State->m_Dev, descriptorWrites.Size(), descriptorWrites.Data(), 0, nullptr);
    });
}

} // namespace

void Rhi::Init(const RhiInitDesc &rhiDesc)
{
    {
        auto rootAlloc = rhiDesc.rootAlloc;
        auto permanentAlloc = rootAlloc.PushSubAlloc(16_KiB);
        auto transientAlloc = rootAlloc.PushSubAlloc(16_KiB);
        g_State = permanentAlloc.Push(RhiGlobalState{});
        g_State->rootAlloc = rootAlloc;
        g_State->permanentAlloc = permanentAlloc;
        g_State->transientAlloc = transientAlloc;
    }

    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    NYLA_ASSERT(rhiDesc.limits.numFramesInFlight <= kRhiMaxNumFramesInFlight);

    g_State->m_Flags = rhiDesc.flags;
    g_State->m_Limits = rhiDesc.limits;

    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "nyla",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "nyla",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    auto &enabledInstanceExtensions = g_State->transientAlloc.PushVec<const char *, 5>();
    enabledInstanceExtensions.PushBack(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__linux__)
    enabledInstanceExtensions.PushBack(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#else
    enabledInstanceExtensions.PushBack(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    auto &instanceLayers = g_State->transientAlloc.PushVec<const char *, 2>();
    if (kRhiValidations)
    {
        instanceLayers.PushBack("VK_LAYER_KHRONOS_validation");
    }

    auto &validationEnabledFeatures = g_State->transientAlloc.PushVec<VkValidationFeatureEnableEXT, 4>();
    auto &validationDisabledFeatures = g_State->transientAlloc.PushVec<VkValidationFeatureDisableEXT, 4>();
    VkValidationFeaturesEXT validationFeatures = {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 0,
        .pEnabledValidationFeatures = validationEnabledFeatures.Data(),
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures = validationDisabledFeatures.Data(),
    };

    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = &validationFeatures,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugMessengerCallback,
        .pUserData = nullptr,
    };

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    auto &layers = g_State->transientAlloc.PushVec<VkLayerProperties, 256>();
    layers.Resize(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.Data());

    {
        auto &instanceExtensions = g_State->transientAlloc.PushVec<VkExtensionProperties, 256>();
        uint32_t instanceExtensionsCount;
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, nullptr);

        instanceExtensions.Resize(instanceExtensionsCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, instanceExtensions.Data());

        NYLA_LOG("");
        NYLA_LOG("%d Instance Extensions available", instanceExtensionsCount);
        for (uint32_t i = 0; i < instanceExtensionsCount; ++i)
        {
            const auto &extension = instanceExtensions[i];
            NYLA_LOG("%s", extension.extensionName);
        }
    }

    NYLA_LOG("");
    NYLA_LOG("%zd Layers available", layers.Size());
    for (uint32_t i = 0; i < layerCount; ++i)
    {
        const auto &layer = layers[i];
        NYLA_LOG("    %s", layer.layerName);

        auto &layerExtensions = g_State->transientAlloc.PushVec<VkExtensionProperties, 256>();
        uint32_t layerExtensionProperties;
        vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, nullptr);

        layerExtensions.Resize(layerExtensionProperties);
        vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, layerExtensions.Data());

        for (uint32_t i = 0; i < layerExtensionProperties; ++i)
        {
            const auto &extension = layerExtensions[i];
            NYLA_LOG("        %s", extension.extensionName);
        }
    }

    VkDebugUtilsMessengerEXT debugMessenger{};

    const void *instancePNext = nullptr;
    if constexpr (kRhiValidations)
    {
        if constexpr (false)
        {
            validationEnabledFeatures.PushBack(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
            validationEnabledFeatures.PushBack(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            validationEnabledFeatures.PushBack(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
            validationEnabledFeatures.PushBack(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
            validationFeatures.enabledValidationFeatureCount = validationEnabledFeatures.Size();

            validationDisabledFeatures.PushBack(VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT);
            validationFeatures.disabledValidationFeatureCount = validationEnabledFeatures.Size();
        }

        instancePNext = &debugMessengerCreateInfo;
        enabledInstanceExtensions.PushBack(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        enabledInstanceExtensions.PushBack(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }

    const VkInstanceCreateInfo instanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = instancePNext,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(instanceLayers.Size()),
        .ppEnabledLayerNames = instanceLayers.Data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabledInstanceExtensions.Size()),
        .ppEnabledExtensionNames = enabledInstanceExtensions.Data(),
    };
    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &g_State->m_Instance));

    if constexpr (kRhiValidations)
    {
        auto createDebugUtilsMessenger = VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT);
        VK_CHECK(createDebugUtilsMessenger(g_State->m_Instance, &debugMessengerCreateInfo, nullptr, &debugMessenger));
    }

    uint32_t numPhysDevices = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(g_State->m_Instance, &numPhysDevices, nullptr));

    auto &physDevs = g_State->transientAlloc.PushVec<VkPhysicalDevice, 16>();
    physDevs.Resize(numPhysDevices);
    VK_CHECK(vkEnumeratePhysicalDevices(g_State->m_Instance, &numPhysDevices, physDevs.Data()));

    auto &deviceExtensions = g_State->transientAlloc.PushVec<const char *, 256>();
    deviceExtensions.PushBack(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.PushBack(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
    deviceExtensions.PushBack(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);

    if constexpr (kRhiCheckpoints)
    {
        deviceExtensions.PushBack(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    }

    for (VkPhysicalDevice physDev : physDevs)
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, nullptr);
        auto &extensions = g_State->transientAlloc.PushVec<VkExtensionProperties, 256>();
        extensions.Resize(256);
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, extensions.Data());

        uint32_t missingExtensions = deviceExtensions.Size();
        for (auto &deviceExtension : deviceExtensions)
        {
            for (uint32_t i = 0; i < extensionCount; ++i)
            {
                if (AsStr<const char *>(extensions[i].extensionName) == AsStr(deviceExtension))
                {
                    NYLA_LOG("Found device extension: %s", deviceExtension);
                    --missingExtensions;
                    break;
                }
            }
        }

        if (missingExtensions)
        {
            NYLA_LOG("Missing %d extensions", missingExtensions);
            continue;
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDev, &props);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

        uint32_t queueFamilyPropCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyPropCount, nullptr);
        auto &queueFamilyProperties = g_State->transientAlloc.PushVec<VkQueueFamilyProperties, 256>();
        queueFamilyProperties.Resize(queueFamilyPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyPropCount, queueFamilyProperties.Data());

        constexpr static uint32_t kInvalidQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
        uint32_t graphicsQueueIndex = kInvalidQueueFamilyIndex;
        uint32_t transferQueueIndex = kInvalidQueueFamilyIndex;

        for (size_t i = 0; i < queueFamilyPropCount; ++i)
        {
            VkQueueFamilyProperties &props = queueFamilyProperties[i];
            if (!props.queueCount)
            {
                continue;
            }

            if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                if (graphicsQueueIndex == kInvalidQueueFamilyIndex)
                {
                    graphicsQueueIndex = i;
                }
                continue;
            }

            if (props.queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                continue;
            }

            if (props.queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                if (transferQueueIndex == kInvalidQueueFamilyIndex)
                {
                    transferQueueIndex = i;
                }
                continue;
            }
        }

        if (graphicsQueueIndex == kInvalidQueueFamilyIndex)
        {
            continue;
        }

        // TODO: pick best device
        g_State->m_PhysDev = physDev;
        g_State->m_PhysDevProps = props;
        g_State->m_PhysDevMemProps = memProps;
        g_State->m_GraphicsQueue.queueFamilyIndex = graphicsQueueIndex;
        g_State->m_TransferQueue.queueFamilyIndex = transferQueueIndex;

        break;
    }

    NYLA_ASSERT(g_State->m_PhysDev);

    const float queuePriority = 1.0f;
    auto &queueCreateInfos = g_State->transientAlloc.PushVec<VkDeviceQueueCreateInfo, 2>();
    if (g_State->m_TransferQueue.queueFamilyIndex == kInvalidIndex)
    {
        queueCreateInfos.PushBack(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = g_State->m_GraphicsQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }
    else
    {
        queueCreateInfos.PushBack(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = g_State->m_GraphicsQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });

        queueCreateInfos.PushBack(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = g_State->m_TransferQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }

    VkPhysicalDeviceVulkan14Features v14{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
    };

    VkPhysicalDeviceVulkan13Features v13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &v14,
        .shaderDemoteToHelperInvocation = true,
        .synchronization2 = true,
        .dynamicRendering = true,
    };

    VkPhysicalDeviceVulkan12Features v12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &v13,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .descriptorBindingSampledImageUpdateAfterBind = true,
        .descriptorBindingUpdateUnusedWhilePending = true,
        .descriptorBindingPartiallyBound = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .timelineSemaphore = true,
    };

    VkPhysicalDeviceVulkan11Features v11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &v12,
    };

    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInputDynamicStateFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &v11,
        .vertexInputDynamicState = true,
    };

    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR fifoLatestReadyFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext = &vertexInputDynamicStateFeatures,
        .presentModeFifoLatestReady = true,
    };

    const VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &fifoLatestReadyFeatures,
        .features = {},
    };

    const VkDeviceCreateInfo deviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.Size()),
        .pQueueCreateInfos = queueCreateInfos.Data(),
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.Size()),
        .ppEnabledExtensionNames = deviceExtensions.Data(),
    };
    VK_CHECK(vkCreateDevice(g_State->m_PhysDev, &deviceCreateInfo, nullptr, &g_State->m_Dev));

    vkGetDeviceQueue(g_State->m_Dev, g_State->m_GraphicsQueue.queueFamilyIndex, 0, &g_State->m_GraphicsQueue.queue);

    if (g_State->m_TransferQueue.queueFamilyIndex == kInvalidIndex)
    {
        g_State->m_TransferQueue.queueFamilyIndex = g_State->m_GraphicsQueue.queueFamilyIndex;
        g_State->m_TransferQueue.queue = g_State->m_GraphicsQueue.queue;
    }
    else
    {
        vkGetDeviceQueue(g_State->m_Dev, g_State->m_TransferQueue.queueFamilyIndex, 0, &g_State->m_TransferQueue.queue);
    }

    auto initQueue = [](DeviceQueue &queue, RhiQueueType queueType, std::span<RhiCmdList> cmd) -> void {
        const VkCommandPoolCreateInfo commandPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue.queueFamilyIndex,
        };
        VK_CHECK(vkCreateCommandPool(g_State->m_Dev, &commandPoolCreateInfo, nullptr, &queue.cmdPool));

        queue.timeline = CreateTimeline(0);
        queue.timelineNext = 1;

        for (auto &i : cmd)
        {
            i = CreateCmdList(queueType);
        }
    };
    initQueue(g_State->m_GraphicsQueue, RhiQueueType::Graphics,
              std::span{g_State->m_GraphicsQueueCmd.Data(), g_State->m_Limits.numFramesInFlight});
    initQueue(g_State->m_TransferQueue, RhiQueueType::Transfer, std::span{&g_State->m_TransferQueueCmd, 1});

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (size_t i = 0; i < g_State->m_Limits.numFramesInFlight; ++i)
    {
        VK_CHECK(vkCreateSemaphore(g_State->m_Dev, &semaphoreCreateInfo, nullptr,
                                   g_State->m_SwapchainAcquireSemaphores.Data() + i));
    }
    for (size_t i = 0; i < kRhiMaxNumSwapchainTextures; ++i)
    {
        VK_CHECK(vkCreateSemaphore(g_State->m_Dev, &semaphoreCreateInfo, nullptr,
                                   g_State->m_RenderFinishedSemaphores.Data() + i));
    }

#if defined(__linux__)
    const VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = xcb_connect(nullptr, nullptr),
        .window = LinuxX11Platform::WinGetHandle(),
    };
    VK_CHECK(vkCreateXcbSurfaceKHR(g_State->m_Instance, &surfaceCreateInfo, g_State->vkAlloc, &g_State->m_Surface));
#else
    const VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = WindowsPlatform::GetHInstance(),
        .hwnd = WindowsPlatform::WinGetHandle(),
    };
    vkCreateWin32SurfaceKHR(g_State->m_Instance, &surfaceCreateInfo, g_State->vkAlloc, &g_State->m_Surface);
#endif

    CreateSwapchain();

    //

    const Array<VkDescriptorPoolSize, 4> descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 8},
    };

    const VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 16,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.Size()),
        .pPoolSizes = descriptorPoolSizes.Data(),
    };
    vkCreateDescriptorPool(g_State->m_Dev, &descriptorPoolCreateInfo, nullptr, &g_State->m_DescriptorPool);

    auto initDescriptorTable = [](DescriptorTable &table,
                                  const VkDescriptorSetLayoutCreateInfo &layoutCreateInfo) -> void {
        VK_CHECK(vkCreateDescriptorSetLayout(g_State->m_Dev, &layoutCreateInfo, g_State->vkAlloc, &table.layout));

        const VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = g_State->m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &table.layout,
        };

        VK_CHECK(vkAllocateDescriptorSets(g_State->m_Dev, &descriptorSetAllocInfo, &table.set));
    };

    { // Constants

        const Array<VkDescriptorSetLayoutBinding, 4> descriptorLayoutBindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };

        const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = descriptorLayoutBindings.Size32(),
            .pBindings = descriptorLayoutBindings.Data(),
        };

        initDescriptorTable(g_State->m_ConstantsDescriptorTable, descriptorSetLayoutCreateInfo);

        const uint32_t bufferSize =
            g_State->m_Limits.numFramesInFlight *
            (CbvOffset(g_State->m_Limits.frameConstantSize) +
             g_State->m_Limits.maxPassCount * CbvOffset(g_State->m_Limits.passConstantSize) +
             g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.drawConstantSize) +
             g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.largeDrawConstantSize));
        NYLA_LOG("Constants Buffer Size: %fmb", (double)bufferSize / 1024.0 / 1024.0);

        g_State->m_ConstantsUniformBuffer = CreateBuffer(RhiBufferDesc{
            .size = bufferSize,
            .bufferUsage = RhiBufferUsage::Uniform,
            .memoryUsage = RhiMemoryUsage::CpuToGpu,
        });
        const VkBuffer &vulkanBuffer = g_State->m_Buffers.ResolveData(g_State->m_ConstantsUniformBuffer).buffer;

        const Array<VkDescriptorBufferInfo, 4> bufferInfos{
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(g_State->m_Limits.frameConstantSize),
            },
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(g_State->m_Limits.passConstantSize),
            },
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(g_State->m_Limits.drawConstantSize),
            },
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(g_State->m_Limits.largeDrawConstantSize),
            },
        };

        auto &descriptorWrites = g_State->transientAlloc.PushVec<VkWriteDescriptorSet, bufferInfos.Size()>();
        for (uint32_t i = 0; i < bufferInfos.Size(); ++i)
        {
            const VkDescriptorBufferInfo &bufferInfo = bufferInfos[i];
            if (bufferInfo.range)
            {
                descriptorWrites.PushBack(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = g_State->m_ConstantsDescriptorTable.set,
                    .dstBinding = i,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .pBufferInfo = &bufferInfo,
                });
            }
        }

        vkUpdateDescriptorSets(g_State->m_Dev, descriptorWrites.Size(), descriptorWrites.Data(), 0, nullptr);
    }

    { // Textures

        const VkDescriptorBindingFlags bindingFlags =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 1,
            .pBindingFlags = &bindingFlags,
        };

        const VkDescriptorSetLayoutBinding descriptorLayoutBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = g_State->m_Limits.numTextureViews,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsCreateInfo,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &descriptorLayoutBinding,
        };

        initDescriptorTable(g_State->m_TexturesDescriptorTable, descriptorSetLayoutCreateInfo);
    }

    { // Samplers

        const VkDescriptorBindingFlags bindingFlags =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 1,
            .pBindingFlags = &bindingFlags,
        };

        const VkDescriptorSetLayoutBinding descriptorLayoutBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = g_State->m_Limits.numSamplers,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsCreateInfo,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &descriptorLayoutBinding,
        };

        initDescriptorTable(g_State->m_SamplersDescriptorTable, descriptorSetLayoutCreateInfo);
    }
}

auto Rhi::CreateBuffer(const RhiBufferDesc &desc) -> RhiBuffer
{
    VulkanBufferData bufferData{
        .size = desc.size,
        .memoryUsage = desc.memoryUsage,
    };

    const VkBufferCreateInfo bufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = ConvertBufferUsageIntoVkBufferUsageFlags(desc.bufferUsage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(g_State->m_Dev, &bufferCreateInfo, nullptr, &bufferData.buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_State->m_Dev, bufferData.buffer, &memRequirements);

    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(memRequirements, ConvertMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage));
    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    VK_CHECK(vkAllocateMemory(g_State->m_Dev, &memoryAllocInfo, nullptr, &bufferData.memory));
    VK_CHECK(vkBindBufferMemory(g_State->m_Dev, bufferData.buffer, bufferData.memory, 0));

    return g_State->m_Buffers.Acquire(bufferData);
}

void Rhi::NameBuffer(RhiBuffer buf, Str name)
{
    const VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buf);
    VulkanNameHandle(VK_OBJECT_TYPE_BUFFER, (uint64_t)bufferData.buffer, name);
}

void Rhi::DestroyBuffer(RhiBuffer buffer)
{
    VulkanBufferData bufferData = g_State->m_Buffers.ReleaseData(buffer);

    if (bufferData.mapped)
    {
        vkUnmapMemory(g_State->m_Dev, bufferData.memory);
    }
    vkDestroyBuffer(g_State->m_Dev, bufferData.buffer, nullptr);
    vkFreeMemory(g_State->m_Dev, bufferData.memory, nullptr);
}

auto Rhi::GetBufferSize(RhiBuffer buffer) -> uint64_t
{
    return g_State->m_Buffers.ResolveData(buffer).size;
}

auto Rhi::MapBuffer(RhiBuffer buffer) -> char *
{
    const VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buffer);
    if (!bufferData.mapped)
    {
        vkMapMemory(g_State->m_Dev, bufferData.memory, 0, VK_WHOLE_SIZE, 0, (void **)&bufferData.mapped);
    }

    return bufferData.mapped;
}

void Rhi::UnmapBuffer(RhiBuffer buffer)
{
    VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buffer);
    if (bufferData.mapped)
    {
        vkUnmapMemory(g_State->m_Dev, bufferData.memory);
        bufferData.mapped = nullptr;
    }
}

void Rhi::CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                        uint32_t size)
{
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanBufferData &dstBufferData = g_State->m_Buffers.ResolveData(dst);
    VulkanBufferData &srcBufferData = g_State->m_Buffers.ResolveData(src);

    EnsureHostWritesVisible(cmdbuf, srcBufferData);

    const VkBufferCopy region{
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = size,
    };
    vkCmdCopyBuffer(cmdbuf, srcBufferData.buffer, dstBufferData.buffer, 1, &region);
}

void Rhi::CmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState)
{
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;
    VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buffer);

    VulkanBufferStateSyncInfo oldSync = VulkanBufferStateGetSyncInfo(bufferData.state);
    VulkanBufferStateSyncInfo newSync = VulkanBufferStateGetSyncInfo(newState);

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = oldSync.stage,
        .srcAccessMask = oldSync.access,
        .dstStageMask = newSync.stage,
        .dstAccessMask = newSync.access,
        .buffer = bufferData.buffer,
        .size = GetBufferSize(buffer),
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    bufferData.state = newState;
}

void Rhi::CmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer)
{
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;
    VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buffer);

    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = bufferData.buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);
}

void Rhi::BufferMarkWritten(RhiBuffer buffer, uint32_t offset, uint32_t size)
{
    VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buffer);

    if (bufferData.dirty)
    {
        bufferData.dirtyBegin = std::min(bufferData.dirtyBegin, offset);
        bufferData.dirtyEnd = std::max(bufferData.dirtyBegin, offset + size);
    }
    else
    {
        bufferData.dirty = true;
        bufferData.dirtyBegin = offset;
        bufferData.dirtyEnd = offset + size;
    }
}

auto Rhi::GetMinUniformBufferOffsetAlignment() -> uint32_t
{
    return g_State->m_PhysDevProps.limits.minUniformBufferOffsetAlignment;
}

auto Rhi::GetOptimalBufferCopyOffsetAlignment() -> uint32_t
{
    return g_State->m_PhysDevProps.limits.optimalBufferCopyOffsetAlignment;
}

auto Rhi::CreateCmdList(RhiQueueType queueType) -> RhiCmdList
{
    VkCommandPool cmdPool = GetDeviceQueue(queueType).cmdPool;
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(g_State->m_Dev, &allocInfo, &commandBuffer));

    VulkanCmdListData cmdData{
        .cmdbuf = commandBuffer,
        .queueType = queueType,
    };

    RhiCmdList cmd = g_State->m_CmdLists.Acquire(cmdData);
    return cmd;
}

void Rhi::ResetCmdList(RhiCmdList cmd)
{
    VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    VK_CHECK(vkResetCommandBuffer(cmdbuf, 0));

    cmdData.frameConstantHead =
        GetFrameIndex() * (CbvOffset(g_State->m_Limits.frameConstantSize) +
                           g_State->m_Limits.maxPassCount * CbvOffset(g_State->m_Limits.passConstantSize) +
                           g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.drawConstantSize) +
                           g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.largeDrawConstantSize));

    cmdData.passConstantHead = cmdData.frameConstantHead + g_State->m_Limits.frameConstantSize;

    cmdData.drawConstantHead =
        cmdData.passConstantHead + (g_State->m_Limits.maxPassCount * CbvOffset(g_State->m_Limits.passConstantSize));

    cmdData.largeDrawConstantHead =
        cmdData.drawConstantHead + (g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.drawConstantSize));
}

void Rhi::NameCmdList(RhiCmdList cmd, Str name)
{
    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    VulkanNameHandle(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmdData.cmdbuf, name);
}

void Rhi::DestroyCmdList(RhiCmdList cmd)
{
    VulkanCmdListData cmdData = g_State->m_CmdLists.ReleaseData(cmd);
    VkCommandPool cmdPool = GetDeviceQueue(cmdData.queueType).cmdPool;
    vkFreeCommandBuffers(g_State->m_Dev, cmdPool, 1, &cmdData.cmdbuf);
}

auto Rhi::CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return data;
    }

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkCmdSetCheckpointNV);
    fn(cmdData.cmdbuf, reinterpret_cast<void *>(data));

    return data;
}

auto Rhi::GetLastCheckpointData(RhiQueueType queueType) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return -1;
    }

    uint32_t dataCount = 1;
    VkCheckpointDataNV data{};

    const VkQueue queue = GetDeviceQueue(queueType).queue;

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkGetQueueCheckpointDataNV);
    fn(queue, &dataCount, &data);

    return (uint64_t)data.pCheckpointMarker;
}

auto Rhi::FrameBegin() -> RhiCmdList
{
    if (g_State->m_RecreateSwapchain)
    {
        vkDeviceWaitIdle(g_State->m_Dev);
        CreateSwapchain();
        g_State->m_SwapchainUsable = true;
        g_State->m_RecreateSwapchain = false;
    }
    else
    {
        WaitTimeline(g_State->m_GraphicsQueue.timeline, g_State->m_GraphicsQueueCmdDone[g_State->m_FrameIndex]);
    }

    if (g_State->m_SwapchainUsable)
    {
        const VkResult acquireResult =
            vkAcquireNextImageKHR(g_State->m_Dev, g_State->m_Swapchain, Limits<uint64_t>::Max(),
                                  g_State->m_SwapchainAcquireSemaphores[g_State->m_FrameIndex], VK_NULL_HANDLE,
                                  &g_State->m_SwapchainTextureIndex);

        switch (acquireResult)
        {
        case VK_ERROR_OUT_OF_DATE_KHR: {
            g_State->m_SwapchainUsable = false;
            g_State->m_RecreateSwapchain = true;
            break;
        }

        case VK_SUBOPTIMAL_KHR: {
            g_State->m_SwapchainUsable = true;
            g_State->m_RecreateSwapchain = true;
            break;
        }

        default: {
            VK_CHECK(acquireResult);
            g_State->m_RecreateSwapchain = false;
            g_State->m_SwapchainUsable = true;
            break;
        }
        }
    }

    if (!g_State->m_SwapchainUsable)
    {
        vkDeviceWaitIdle(g_State->m_Dev);
    }

    const RhiCmdList cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
    ResetCmdList(cmd);

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmdbuf, &commandBufferBeginInfo));

    return cmd;
}

void Rhi::FrameEnd()
{
    RhiCmdList cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];

    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;

    VK_CHECK(vkEndCommandBuffer(cmdbuf));

    WriteDescriptorTables();

    const Array<VkPipelineStageFlags, 1> waitStages = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSemaphore acquireSemaphore = g_State->m_SwapchainAcquireSemaphores[g_State->m_FrameIndex];
    const VkSemaphore renderFinishedSemaphore = g_State->m_RenderFinishedSemaphores[g_State->m_SwapchainTextureIndex];

    const Array<VkSemaphore, 2> signalSemaphores{
        g_State->m_GraphicsQueue.timeline,
        renderFinishedSemaphore,
    };

    g_State->m_GraphicsQueueCmdDone[g_State->m_FrameIndex] = g_State->m_GraphicsQueue.timelineNext++;

    const VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = signalSemaphores.Size(),
        .pSignalSemaphoreValues = &g_State->m_GraphicsQueueCmdDone[g_State->m_FrameIndex],
    };

    if (g_State->m_SwapchainUsable)
    {
        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timelineSubmitInfo,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquireSemaphore,
            .pWaitDstStageMask = waitStages.Data(),
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdbuf,
            .signalSemaphoreCount = signalSemaphores.Size(),
            .pSignalSemaphores = signalSemaphores.Data(),
        };
        VK_CHECK(vkQueueSubmit(g_State->m_GraphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE));

        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &g_State->m_Swapchain,
            .pImageIndices = &g_State->m_SwapchainTextureIndex,
        };

        const VkResult presentResult = vkQueuePresentKHR(g_State->m_GraphicsQueue.queue, &presentInfo);
    }

    g_State->m_FrameIndex = (g_State->m_FrameIndex + 1) % g_State->m_Limits.numFramesInFlight;
}

auto Rhi::FrameGetCmdList() -> RhiCmdList
{ // TODO: get rid of this
    return g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
}

void Rhi::PassBegin(RhiPassDesc desc)
{
    RhiCmdList cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;

    const VulkanTextureViewData &rtvData = g_State->m_RenderTargetViews.ResolveData(desc.rtv);
    const VulkanTextureData &renderTargetData = g_State->m_Textures.ResolveData(rtvData.texture);

    const VkRenderingAttachmentInfo colorAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = rtvData.imageView,
        .imageLayout = renderTargetData.layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {renderTargetData.extent.width, renderTargetData.extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
    };

    VkRenderingAttachmentInfo depthAttachmentInfo{};

    if (HandleIsSet(desc.dsv))
    {
        const VulkanTextureViewData &dsvData = g_State->m_DepthStencilViews.ResolveData(desc.dsv);
        const VulkanTextureData &depthStencilData = g_State->m_Textures.ResolveData(dsvData.texture);

        depthAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = dsvData.imageView,
            .imageLayout = depthStencilData.layout,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {{{1.0f, 1.0f, 1.0f, 1.0f}}},
        };
        renderingInfo.pDepthAttachment = &depthAttachmentInfo;
    }

    vkCmdBeginRendering(cmdbuf, &renderingInfo);

    const VkViewport viewport{
        .x = 0.f,
        .y = static_cast<float>(renderTargetData.extent.height),
        .width = static_cast<float>(renderTargetData.extent.width),
        .height = -static_cast<float>(renderTargetData.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = {renderTargetData.extent.width, renderTargetData.extent.height},
    };
    vkCmdSetScissor(cmdbuf, 0, 1, &scissor);
}

void Rhi::PassEnd()
{
    RhiCmdList cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
    VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    vkCmdEndRendering(cmdbuf);

    cmdData.passConstantHead += CbvOffset(g_State->m_Limits.passConstantSize);
}

auto Rhi::CreateShader(const RhiShaderDesc &desc) -> RhiShader
{
    Span<uint32_t> spv = g_State->rootAlloc.PushCopySpan<uint32_t>(desc.code);
    MemCpy(spv.Data(), desc.code.Data(), desc.code.Size());

    const RhiShader handle = g_State->m_Shaders.Acquire(VulkanShaderData{.spv = spv});
    VulkanShaderData &shaderData = g_State->m_Shaders.ResolveData(handle);

    return handle;
}

void Rhi::DestroyShader(RhiShader shader)
{
    g_State->m_Shaders.ReleaseData(shader);

#if 0
    VkShaderModule shaderModule = g_State->m_Shaders.ReleaseData(shader);
    vkDestroyShaderModule(g_State->m_Dev, shaderModule, nullptr);
#endif
}

void Rhi::DestroyGraphicsPipeline(RhiGraphicsPipeline pipeline)
{
    auto pipelineData = g_State->m_GraphicsPipelines.ReleaseData(pipeline);
    vkDeviceWaitIdle(g_State->m_Dev);

    if (pipelineData.layout)
    {
        vkDestroyPipelineLayout(g_State->m_Dev, pipelineData.layout, nullptr);
    }
    if (pipelineData.pipeline)
    {
        vkDestroyPipeline(g_State->m_Dev, pipelineData.pipeline, nullptr);
    }
}

void Rhi::CmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline)
{
    VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VulkanPipelineData &pipelineData = g_State->m_GraphicsPipelines.ResolveData(pipeline);

    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.pipeline);
    cmdData.boundGraphicsPipeline = pipeline;

    Array<VkDescriptorSet, 2> descriptorSets{
        g_State->m_TexturesDescriptorTable.set,
        g_State->m_SamplersDescriptorTable.set,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 1, descriptorSets.Size(),
                            descriptorSets.Data(), 0, nullptr);
}

void Rhi::CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data)
{
    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = g_State->m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    vkCmdPushConstants(cmdData.cmdbuf, pipelineData.layout, ConvertShaderStageIntoVkShaderStageFlags(stage), offset,
                       data.size(), data.data());
}

void Rhi::CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                               std::span<const uint64_t> offsets)
{
    NYLA_ASSERT(buffers.size() == offsets.size());
    NYLA_ASSERT(buffers.size() <= 4U);

    Array<VkBuffer, 4> vkBufs;
    Array<VkDeviceSize, 4> vkOffsets;
    for (uint32_t i = 0; i < buffers.size(); ++i)
    {
        vkBufs[i] = g_State->m_Buffers.ResolveData(buffers[i]).buffer;
        vkOffsets[i] = offsets[i];
    }

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    vkCmdBindVertexBuffers(cmdData.cmdbuf, firstBinding, buffers.size(), vkBufs.Data(), vkOffsets.Data());
}

void Rhi::CmdBindIndexBuffer(RhiCmdList cmd, RhiBuffer buffer, uint64_t offset)
{
    const VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(buffer);

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    vkCmdBindIndexBuffer(cmdData.cmdbuf, bufferData.buffer, offset, VkIndexType::VK_INDEX_TYPE_UINT16);
}

void Rhi::CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                  uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    CmdDrawInternal(cmdData);
    vkCmdDraw(cmdData.cmdbuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Rhi::CmdDrawIndexed(RhiCmdList cmd, uint32_t indexCount, int32_t vertexOffset, uint32_t instanceCount,
                         uint32_t firstIndex, uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);
    CmdDrawInternal(cmdData);
    vkCmdDrawIndexed(cmdData.cmdbuf, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

auto Rhi::CreateSampler(const RhiSamplerDesc &desc) -> RhiSampler
{
    const VkSamplerCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = ConvertFilter(desc.magFilter),
        .minFilter = ConvertFilter(desc.minFilter),
        .mipmapMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = ConvertSamplerAddressMode(desc.addressModeU),
        .addressModeV = ConvertSamplerAddressMode(desc.addressModeV),
        .addressModeW = ConvertSamplerAddressMode(desc.addressModeW),
        .mipLodBias = 0.f,
        .anisotropyEnable = false,
        .maxAnisotropy = 0.f,
        .minLod = 0.f,
        .maxLod = 0.f,
    };

    VulkanSamplerData samplerData{};
    VK_CHECK(vkCreateSampler(g_State->m_Dev, &createInfo, g_State->vkAlloc, &samplerData.sampler));

    return g_State->m_Samplers.Acquire(samplerData);
}

void Rhi::DestroySampler(RhiSampler sampler)
{
    VulkanSamplerData samplerData = g_State->m_Samplers.ReleaseData(sampler);
    vkDestroySampler(g_State->m_Dev, samplerData.sampler, g_State->vkAlloc);
}

auto Rhi::GetBackbufferView() -> RhiRenderTargetView
{
    return g_State->m_SwapchainRTVs[g_State->m_SwapchainTextureIndex];
}

auto Rhi::CreateTexture(const RhiTextureDesc &desc) -> RhiTexture
{
    VulkanTextureData textureData{
        .extent = {desc.width, desc.height, 1},
    };

    textureData.format = ConvertTextureFormatIntoVkFormat(desc.format, &textureData.aspectMask);

    VkMemoryPropertyFlags memoryPropertyFlags = ConvertMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage);

    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureData.format,
        .extent = {desc.width, desc.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = ConvertTextureUsageToVkImageUsageFlags(desc.usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(g_State->m_Dev, &imageCreateInfo, g_State->vkAlloc, &textureData.image));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(g_State->m_Dev, textureData.image, &memoryRequirements);

    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(memoryRequirements, memoryPropertyFlags),
    };
    vkAllocateMemory(g_State->m_Dev, &memoryAllocInfo, g_State->vkAlloc, &textureData.memory);
    vkBindImageMemory(g_State->m_Dev, textureData.image, textureData.memory, 0);

    return g_State->m_Textures.Acquire(textureData);
}

auto Rhi::CreateSampledTextureView(const RhiTextureViewDesc &desc) -> RhiSampledTextureView
{
    VulkanTextureData &textureData = g_State->m_Textures.ResolveData(desc.texture);

    VulkanTextureViewData textureViewData{
        .texture = desc.texture,

        // TODO: expose these as well!
        .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    if (desc.format == RhiTextureFormat::None)
        textureViewData.format = textureData.format;
    else
        textureViewData.format = ConvertTextureFormatIntoVkFormat(desc.format, nullptr);

    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = textureData.image,
        .viewType = textureViewData.imageViewType,
        .format = textureViewData.format,
        .subresourceRange = textureViewData.subresourceRange,
    };

    VK_CHECK(vkCreateImageView(g_State->m_Dev, &imageViewCreateInfo, g_State->vkAlloc, &textureViewData.imageView));

    const RhiSampledTextureView view = g_State->m_SampledTextureViews.Acquire(textureViewData);
    return view;
}

auto Rhi::CreateRenderTargetView(const RhiRenderTargetViewDesc &desc) -> RhiRenderTargetView
{
    VulkanTextureData &textureData = g_State->m_Textures.ResolveData(desc.texture);

    VulkanTextureViewData renderTargetViewData{
        .texture = desc.texture,

        // TODO: expose these as well!
        .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    if (desc.format == RhiTextureFormat::None)
        renderTargetViewData.format = textureData.format;
    else
        renderTargetViewData.format = ConvertTextureFormatIntoVkFormat(desc.format, nullptr);

    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = textureData.image,
        .viewType = renderTargetViewData.imageViewType,
        .format = renderTargetViewData.format,
        .subresourceRange = renderTargetViewData.subresourceRange,
    };

    VK_CHECK(
        vkCreateImageView(g_State->m_Dev, &imageViewCreateInfo, g_State->vkAlloc, &renderTargetViewData.imageView));
    const RhiRenderTargetView rtv = g_State->m_RenderTargetViews.Acquire(renderTargetViewData);
    return rtv;
}

auto Rhi::CreateDepthStencilView(const RhiDepthStencilViewDesc &desc) -> RhiDepthStencilView
{
    VulkanTextureData &textureData = g_State->m_Textures.ResolveData(desc.texture);

    VulkanTextureViewData dsvData{
        .texture = desc.texture,

        // TODO: expose these as well!
        .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    if (desc.format == RhiTextureFormat::None)
        dsvData.format = textureData.format;
    else
        dsvData.format = ConvertTextureFormatIntoVkFormat(desc.format, nullptr);

    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = textureData.image,
        .viewType = dsvData.imageViewType,
        .format = dsvData.format,
        .subresourceRange = dsvData.subresourceRange,
    };

    VK_CHECK(vkCreateImageView(g_State->m_Dev, &imageViewCreateInfo, g_State->vkAlloc, &dsvData.imageView));
    const RhiDepthStencilView dsv = g_State->m_DepthStencilViews.Acquire(dsvData);
    return dsv;
}

auto Rhi::GetTexture(RhiSampledTextureView srv) -> RhiTexture
{
    return g_State->m_SampledTextureViews.ResolveData(srv).texture;
}

auto Rhi::GetTexture(RhiRenderTargetView rtv) -> RhiTexture
{
    return g_State->m_RenderTargetViews.ResolveData(rtv).texture;
}

auto Rhi::GetTexture(RhiDepthStencilView dsv) -> RhiTexture
{
    return g_State->m_DepthStencilViews.ResolveData(dsv).texture;
}

auto Rhi::GetTextureInfo(RhiTexture texture) -> RhiTextureInfo
{
    const VulkanTextureData &textureData = g_State->m_Textures.ResolveData(texture);
    return {
        .width = textureData.extent.width,
        .height = textureData.extent.height,
        .format = ConvertVkFormatIntoTextureFormat(textureData.format),
    };
}

void Rhi::CmdTransitionTexture(RhiCmdList cmd, RhiTexture texture, RhiTextureState newState)
{
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &textureData = g_State->m_Textures.ResolveData(texture);

    const VulkanTextureStateSyncInfo newSyncInfo = VulkanTextureStateGetSyncInfo(newState);
    if (newSyncInfo.layout == textureData.layout)
        return;

    const VulkanTextureStateSyncInfo oldSyncInfo = VulkanTextureStateGetSyncInfo(textureData.state);

    const VkImageMemoryBarrier2 imageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = oldSyncInfo.stage,
        .srcAccessMask = oldSyncInfo.access,
        .dstStageMask = newSyncInfo.stage,
        .dstAccessMask = newSyncInfo.access,
        .oldLayout = textureData.layout,
        .newLayout = newSyncInfo.layout,
        .image = textureData.image,
        .subresourceRange =
            {
                .aspectMask = textureData.aspectMask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &imageMemoryBarrier,
    };
    vkCmdPipelineBarrier2(cmdbuf, &dependencyInfo);

    textureData.state = newState;
    textureData.layout = newSyncInfo.layout;
}

void Rhi::DestroyTexture(RhiTexture texture)
{
    VulkanTextureData textureData = g_State->m_Textures.ReleaseData(texture);

    NYLA_ASSERT(textureData.image);
    vkDestroyImage(g_State->m_Dev, textureData.image, g_State->vkAlloc);

    NYLA_ASSERT(textureData.memory);
    vkFreeMemory(g_State->m_Dev, textureData.memory, g_State->vkAlloc);
}

void Rhi::DestroySampledTextureView(RhiSampledTextureView textureView)
{
    const VulkanTextureViewData &textureViewData = g_State->m_SampledTextureViews.ReleaseData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    vkDestroyImageView(g_State->m_Dev, textureViewData.imageView, g_State->vkAlloc);
}

void Rhi::DestroyRenderTargetView(RhiRenderTargetView textureView)
{
    const VulkanTextureViewData &textureViewData = g_State->m_RenderTargetViews.ReleaseData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    vkDestroyImageView(g_State->m_Dev, textureViewData.imageView, g_State->vkAlloc);
}

void Rhi::DestroyDepthStencilView(RhiDepthStencilView textureView)
{
    const VulkanTextureViewData &textureViewData = g_State->m_DepthStencilViews.ReleaseData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    vkDestroyImageView(g_State->m_Dev, textureViewData.imageView, g_State->vkAlloc);
}

void Rhi::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size)
{
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &dstTextureData = g_State->m_Textures.ResolveData(dst);
    VulkanBufferData &srcBufferData = g_State->m_Buffers.ResolveData(src);

    EnsureHostWritesVisible(cmdbuf, srcBufferData);

    const VkBufferImageCopy region{
        .bufferOffset = srcOffset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = dstTextureData.aspectMask,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = dstTextureData.extent,
    };

    vkCmdCopyBufferToImage(cmdbuf, srcBufferData.buffer, dstTextureData.image, dstTextureData.layout, 1, &region);
}

void Rhi::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiTexture src)
{
    const VkCommandBuffer &cmdbuf = g_State->m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &dstTextureData = g_State->m_Textures.ResolveData(dst);
    VulkanTextureData &srcTextureData = g_State->m_Textures.ResolveData(src);

    const VkImageCopy region{
        .srcSubresource =
            {
                .aspectMask = srcTextureData.aspectMask,
                .layerCount = 1,
            },
        .dstSubresource =
            {
                .aspectMask = dstTextureData.aspectMask,
                .layerCount = 1,
            },
        .extent = srcTextureData.extent,
    };

    vkCmdCopyImage(cmdbuf, srcTextureData.image, srcTextureData.layout, dstTextureData.image, dstTextureData.layout, 1,
                   &region);
}

//

class SpirvShaderManager
{
  public:
    enum class StorageClass
    {
        Input,
        Output,
    };

    SpirvShaderManager(Span<uint32_t> view, RhiShaderStage stage) : m_SpvView{view}, m_Stage{stage}
    {
        auto end = m_SpvView.end();
        for (auto it = m_SpvView.begin(); it != end; ++it)
        {
            auto op = spv::Op(it.Op());

            switch (op)
            {

#define X(name)                                                                                                        \
    case spv::name: {                                                                                                  \
        name(it);                                                                                                      \
        break;                                                                                                         \
    }
                X(OpEntryPoint)
                X(OpExtension)
                X(OpDecorate)
                X(OpDecorateString)
                X(OpVariable)
            }
        }
    }

    auto FindLocationBySemantic(Str semantic, StorageClass storageClass, uint32_t *outLocation) -> bool
    {
        uint32_t id;
        if (!FindIdBySemantic(semantic, storageClass, &id))
            return false;

        uint32_t location;
        for (uint32_t i = 0; i < m_Locations.Size(); ++i)
        {
            if (m_Locations[i].id == id && CheckStorageClass(id, storageClass))
            {
                *outLocation = m_Locations[i].location;
                return true;
            }
        }

        return false;
    }

    auto FindIdBySemantic(Str querySemantic, StorageClass storageClass, uint32_t *outId) -> bool
    {
        for (uint32_t i = 0; i < m_SemanticDataNames.Size(); ++i)
        {
            const auto &semantic = m_SemanticDataNames[i];
            if (semantic == querySemantic && CheckStorageClass(m_SemanticDataIds[i], storageClass))
            {
                *outId = m_SemanticDataIds[i];
                return true;
            }
        }
        return false;
    }

    auto FindSemanticById(uint32_t id, Str *outSemantic) -> bool
    {
        for (uint32_t i = 0; i < m_SemanticDataIds.Size(); ++i)
        {
            if (m_SemanticDataIds[i] == id)
            {
                *outSemantic = m_SemanticDataNames[i].GetStr();
                return true;
            }
        }
        return false;
    }

    auto RewriteLocationForSemantic(Str semantic, StorageClass storageClass, uint32_t aLocation) -> bool
    {
        uint32_t oldLocation;
        if (!FindLocationBySemantic(semantic, storageClass, &oldLocation))
            return false;

        if (oldLocation == aLocation)
            return true;

        auto end = m_SpvView.end();
        for (auto it = m_SpvView.begin(); it != end; ++it)
        {
            auto op = spv::Op(it.Op());

            if (op != spv::OpDecorate)
                continue;

            SpvOperandReader operandReader = it.GetOperandReader();

            uint32_t id = operandReader.Word();

            if (spv::Decoration(operandReader.Word()) != spv::DecorationLocation)
                continue;
            if (!CheckStorageClass(id, storageClass))
                continue;

            uint32_t &location = operandReader.Word();
            if (location == oldLocation)
            {
                location = aLocation;
                return true;
            }
        }

        return false;
    }

    auto CheckStorageClass(uint32_t id, StorageClass storageClass) -> bool
    {
        switch (storageClass)
        {
        case StorageClass::Input: {
            for (uint32_t i = 0; i < m_InputVariables.Size(); ++i)
                if (id == m_InputVariables[i])
                    return true;
            return false;
        }
        case StorageClass::Output: {
            for (uint32_t i = 0; i < m_OutputVariables.Size(); ++i)
                if (id == m_OutputVariables[i])
                    return true;
            return false;
        }
        default: {
            NYLA_ASSERT(false);
        }
        }
    }

    auto CheckStorageClass(Str semantic, StorageClass storageClass) -> bool
    {
        if (uint32_t id; FindIdBySemantic(semantic, storageClass, &id))
            return CheckStorageClass(id, storageClass);
        return false;
    }

    auto GetInputIds() -> Span<const uint32_t>
    {
        return m_InputVariables.GetSpan().AsConst();
    }

    auto GetSemantics() -> Span<InlineString<16>>
    {
        return m_SemanticDataNames.GetSpan();
    }

  private:
    void OpEntryPoint(Spirview::BasicIterator<uint32_t> it)
    {
        SpvOperandReader operandReader = it.GetOperandReader();

        auto executionModel = spv::ExecutionModel(operandReader.Word());
        switch (executionModel)
        {
        case spv::ExecutionModel::ExecutionModelVertex:
            NYLA_ASSERT(m_Stage == RhiShaderStage::Vertex);
            break;
        case spv::ExecutionModel::ExecutionModelFragment:
            NYLA_ASSERT(m_Stage == RhiShaderStage::Pixel);
            break;
        default:
            NYLA_ASSERT(false);
            break;
        }
    }

    void OpExtension(Spirview::BasicIterator<uint32_t> it)
    {
        SpvOperandReader operandReader = it.GetOperandReader();

        Str name = operandReader.String();
        if (name == AsStr("SPV_GOOGLE_hlsl_functionality1"))
            goto nop;
        if (name == AsStr("SPV_GOOGLE_user_type"))
            goto nop;

        return;
    nop:
        it.MakeNop();
    }

    void OpDecorate(Spirview::BasicIterator<uint32_t> it)
    {
        SpvOperandReader operandReader = it.GetOperandReader();

        uint32_t targetId = operandReader.Word();

        switch (spv::Decoration(operandReader.Word()))
        {
        case spv::Decoration::DecorationLocation: {
            const uint32_t location = operandReader.Word();
            m_Locations.PushBack(LocationData{
                .id = targetId,
                .location = location,
            });
            break;
        }

        case spv::Decoration::DecorationBuiltIn: {
            const uint32_t builtin = operandReader.Word();
            m_Builtin.PushBack(BuiltinData{
                .id = targetId,
                .builtin = builtin,
            });
            break;
        }
        }
    }

    void OpDecorateString(Spirview::BasicIterator<uint32_t> it)
    {
        SpvOperandReader operandReader = it.GetOperandReader();

        const uint32_t targetId = operandReader.Word();

        switch (spv::Decoration(operandReader.Word()))
        {
        case spv::Decoration::DecorationUserSemantic: {
            m_SemanticDataIds.PushBack(targetId);

            auto &name = m_SemanticDataNames.PushBack();
            auto src = operandReader.String();
            MemCpy(name.Data(), src.Data(), src.Size());

            name.AsciiToUpper();

            NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(name.GetStr()));
            break;
        }
        }

        it.MakeNop();
    }

    void OpVariable(Spirview::BasicIterator<uint32_t> it)
    {
        SpvOperandReader operandReader = it.GetOperandReader();

        const uint32_t resultType = operandReader.Word();
        const uint32_t resultId = operandReader.Word();

        switch (static_cast<spv::StorageClass>(operandReader.Word()))
        {
        case spv::StorageClass::StorageClassInput:
            m_InputVariables.PushBack(resultId);
            break;
        case spv::StorageClass::StorageClassOutput:
            m_OutputVariables.PushBack(resultId);
            break;
        default:
            break;
        }
    }

    InlineVec<uint32_t, 8> m_InputVariables;
    InlineVec<uint32_t, 8> m_OutputVariables;

    struct LocationData
    {
        uint32_t id;
        uint32_t location;
    };
    InlineVec<LocationData, 16> m_Locations;

    struct BuiltinData
    {
        uint32_t id;
        uint32_t builtin;
    };
    InlineVec<BuiltinData, 16> m_Builtin;

    InlineVec<uint32_t, 16> m_SemanticDataIds;
    InlineVec<InlineString<16>, 16> m_SemanticDataNames;

    Spirview m_SpvView;
    RhiShaderStage m_Stage{};
};

auto Rhi::CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &desc) -> RhiGraphicsPipeline
{
    return g_State->transientAlloc.Scope(1_KiB, [&](auto &alloc) {
        auto &vertexShaderData = g_State->m_Shaders.ResolveData(desc.vs);
        SpirvShaderManager vsMan(vertexShaderData.spv, RhiShaderStage::Vertex);

        auto &pixelShaderData = g_State->m_Shaders.ResolveData(desc.ps);
        SpirvShaderManager psMan(pixelShaderData.spv, RhiShaderStage::Pixel);

        VulkanPipelineData pipelineData = {
            .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        };

        auto &vertexBindings = alloc.template PushVec<VkVertexInputBindingDescription, 16>();
        for (auto &binding : desc.vertexBindings)
        {
            vertexBindings.PushBack(VkVertexInputBindingDescription{
                .binding = binding.binding,
                .stride = binding.stride,
                .inputRate = ConvertVulkanInputRate(binding.inputRate),
            });
        }

        auto &vertexAttributes = alloc.template PushVec<VkVertexInputAttributeDescription, 16>();
        for (auto &attribute : desc.vertexAttributes)
        {
            uint32_t location;
            if (!vsMan.FindLocationBySemantic(attribute.semantic.GetStr(), SpirvShaderManager::StorageClass::Input,
                                              &location))
                NYLA_ASSERT(false);

            vertexAttributes.PushBack(VkVertexInputAttributeDescription{
                .location = location,
                .binding = attribute.binding,
                .format = ConvertVertexFormatIntoVkFormat(attribute.format),
                .offset = attribute.offset,
            });
        }

        for (uint32_t id : psMan.GetInputIds())
        {
            Str semantic;
            if (!psMan.FindSemanticById(id, &semantic))
                NYLA_ASSERT(false);

            if (semantic.StartWith(AsStr("SV_")))
                continue;

            uint32_t location;
            if (!vsMan.FindLocationBySemantic(semantic, SpirvShaderManager::StorageClass::Output, &location))
                NYLA_ASSERT(false);

            if (!psMan.RewriteLocationForSemantic(semantic, SpirvShaderManager::StorageClass::Input, location))
                NYLA_ASSERT(false);
        }

        auto createShaderModule = [](std::span<const uint32_t> spv) -> VkShaderModule {
            const VkShaderModuleCreateInfo createInfo{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = spv.size_bytes(),
                .pCode = spv.data(),
            };

            VkShaderModule shaderModule;
            VK_CHECK(vkCreateShaderModule(g_State->m_Dev, &createInfo, nullptr, &shaderModule));

            return shaderModule;
        };

        Erase(vertexShaderData.spv, Spirview::kNop);
        Erase(pixelShaderData.spv, Spirview::kNop);

        const Array<VkPipelineShaderStageCreateInfo, 2> stages{
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = createShaderModule(vertexShaderData.spv),
                .pName = "main",
            },
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = createShaderModule(pixelShaderData.spv),
                .pName = "main",
            },
        };

        const VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = vertexBindings.Size32(),
            .pVertexBindingDescriptions = vertexBindings.Data(),
            .vertexAttributeDescriptionCount = vertexAttributes.Size32(),
            .pVertexAttributeDescriptions = vertexAttributes.Data(),
        };

        const VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = ConvertVulkanCullMode(desc.cullMode),
            .frontFace = ConvertVulkanFrontFace(desc.frontFace),
            .lineWidth = 1.0f,
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineMultisampleStateCreateInfo multisamplingCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        const VkPipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };

        const VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
            .blendConstants = {},
        };

        const Array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = dynamicStates.Size(),
            .pDynamicStates = dynamicStates.Data(),
        };

        auto &colorTargetFormats = alloc.template PushVec<VkFormat, 16>();
        for (RhiTextureFormat colorTargetFormat : desc.colorTargetFormats)
        {
            colorTargetFormats.PushBack(ConvertTextureFormatIntoVkFormat(colorTargetFormat, nullptr));
        }

        const VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = colorTargetFormats.Size32(),
            .pColorAttachmentFormats = colorTargetFormats.Data(),
            .depthAttachmentFormat = ConvertTextureFormatIntoVkFormat(desc.depthFormat, nullptr),
        };

#if 0
    NYLA_ASSERT(desc.bindGroupLayoutsCount <= std::size(desc.bindGroupLayouts));
    pipelineData.bindGroupLayoutCount = desc.bindGroupLayoutsCount;
    pipelineData.bindGroupLayouts = desc.bindGroupLayouts;
#endif

#if 0
    NYLA_ASSERT(desc.pushConstantSize <= kRhiMaxPushConstantSize);
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = desc.pushConstantSize,
    };
#endif

        const Array<VkDescriptorSetLayout, 3> descriptorSetLayouts = {
            g_State->m_ConstantsDescriptorTable.layout,
            g_State->m_TexturesDescriptorTable.layout,
            g_State->m_SamplersDescriptorTable.layout,
        };

        const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = descriptorSetLayouts.Size(),
            .pSetLayouts = descriptorSetLayouts.Data(),
#if 0
        .pushConstantRangeCount = desc.pushConstantSize > 0,
        .pPushConstantRanges = &pushConstantRange,
#endif
        };

        vkCreatePipelineLayout(g_State->m_Dev, &pipelineLayoutCreateInfo, nullptr, &pipelineData.layout);

        const VkPipelineDepthStencilStateCreateInfo depthStencilState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = desc.depthTestEnabled,
            .depthWriteEnable = desc.depthWriteEnabled,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };

        const VkGraphicsPipelineCreateInfo pipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = stages.Size(),
            .pStages = stages.Data(),
            .pVertexInputState = &vertexInputStateCreateInfo,
            .pInputAssemblyState = &inputAssemblyCreateInfo,
            .pViewportState = &viewportStateCreateInfo,
            .pRasterizationState = &rasterizerCreateInfo,
            .pMultisampleState = &multisamplingCreateInfo,
            .pDepthStencilState = &depthStencilState,
            .pColorBlendState = &colorBlendingCreateInfo,
            .pDynamicState = &dynamicStateCreateInfo,
            .layout = pipelineData.layout,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        VK_CHECK(vkCreateGraphicsPipelines(g_State->m_Dev, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr,
                                           &pipelineData.pipeline));

        return g_State->m_GraphicsPipelines.Acquire(pipelineData);
    });
}

void Rhi::NameGraphicsPipeline(RhiGraphicsPipeline pipeline, Str name)
{
    const VulkanPipelineData &pipelineData = g_State->m_GraphicsPipelines.ResolveData(pipeline);
    VulkanNameHandle(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelineData.pipeline, name);
}

void Rhi::SetFrameConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= g_State->m_Limits.frameConstantSize);

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.frameConstantHead, data.data(), data.size());
}

void Rhi::SetPassConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= g_State->m_Limits.passConstantSize);

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.passConstantHead, data.data(), data.size());
}

void Rhi::SetDrawConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= g_State->m_Limits.drawConstantSize);

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.drawConstantHead, data.data(), data.size());
}

void Rhi::SetLargeDrawConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= g_State->m_Limits.largeDrawConstantSize);

    const VulkanCmdListData &cmdData = g_State->m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.largeDrawConstantHead, data.data(), data.size());

    if constexpr (false)
    {
        const VulkanBufferData &bufferData = g_State->m_Buffers.ResolveData(g_State->m_ConstantsUniformBuffer);
        const VkMappedMemoryRange range{
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = bufferData.memory,
        };
        vkInvalidateMappedMemoryRanges(g_State->m_Dev, 1, &range);
    }
}

void Rhi::TriggerSwapchainRecreate()
{
    g_State->m_RecreateSwapchain = true;
}

auto Rhi::GetFrameIndex() -> uint32_t
{
    return g_State->m_FrameIndex;
}

auto Rhi::GetNumFramesInFlight() -> uint32_t
{
    return g_State->m_Limits.numFramesInFlight;
}

} // namespace nyla

#undef VK_GET_INSTANCE_PROC_ADDR
#undef VK_CHECK