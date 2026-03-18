#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "nyla/commons/assert.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/log.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "vulkan/vulkan_core.h"

// clang-format off
#if defined(__linux__)
#include "nyla/platform/linux/platform_linux.h"
#include "vulkan/vulkan_xcb.h"
#else
#include "nyla/platform/windows/platform_windows.h"
#include "vulkan/vulkan_win32.h"
#endif
// clang-format on

#define spv_enable_utility_code
#if defined(__linux__)
#include <spirv/unified1/spirv.hpp>
#else
#include <spirv-headers/spirv.hpp>
#endif

#define VK_GET_INSTANCE_PROC_ADDR(name) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(m_Instance, #name))
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
    InlineVec<uint32_t, 1 << 18> *spv;
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
    case nyla::RhiMemoryUsage::Unknown:
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
    case nyla::RhiVertexFormat::None:
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

VkAllocationCallbacks *m_Alloc;

RhiFlags m_Flags;
RhiLimits m_Limits;

VkInstance m_Instance;
VkDevice m_Dev;
VkPhysicalDevice m_PhysDev;
VkPhysicalDeviceProperties m_PhysDevProps;
VkPhysicalDeviceMemoryProperties m_PhysDevMemProps;
VkDescriptorPool m_DescriptorPool;

struct DescriptorTable
{
    VkDescriptorSetLayout layout;
    VkDescriptorSet set;
};

DescriptorTable m_ConstantsDescriptorTable;
RhiBuffer m_ConstantsUniformBuffer;
DescriptorTable m_TexturesDescriptorTable;
DescriptorTable m_SamplersDescriptorTable;

VkSurfaceKHR m_Surface;
VkSwapchainKHR m_Swapchain;
bool m_SwapchainUsable = true;
bool m_RecreateSwapchain = true;

InlineVec<RhiRenderTargetView, kRhiMaxNumSwapchainTextures> m_SwapchainRTVs;
uint32_t m_SwapchainTextureIndex;

std::array<VkSemaphore, kRhiMaxNumSwapchainTextures> m_RenderFinishedSemaphores;
std::array<VkSemaphore, kRhiMaxNumFramesInFlight> m_SwapchainAcquireSemaphores;

DeviceQueue m_GraphicsQueue;
uint32_t m_FrameIndex;
std::array<RhiCmdList, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmd;
std::array<uint64_t, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmdDone;

DeviceQueue m_TransferQueue;
RhiCmdList m_TransferQueueCmd;
uint64_t m_TransferQueueCmdDone;

//

void VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name)
{
    auto nameCopy = std::string{name};
    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = nameCopy.c_str(),
    };

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkSetDebugUtilsObjectNameEXT);
    fn(m_Dev, &nameInfo);
}

auto CbvOffset(uint32_t offset) -> uint32_t
{
    return AlignedUp<uint32_t>(offset, m_PhysDevProps.limits.minUniformBufferOffsetAlignment);
}

auto FindMemoryTypeIndex(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties) -> uint32_t
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    static const VkPhysicalDeviceMemoryProperties memPropertities = [] -> VkPhysicalDeviceMemoryProperties {
        VkPhysicalDeviceMemoryProperties memPropertities;
        vkGetPhysicalDeviceMemoryProperties(m_PhysDev, &memPropertities);
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
        return m_GraphicsQueue;
    case RhiQueueType::Transfer:
        return m_TransferQueue;
    }
    NYLA_ASSERT(false);
    return m_GraphicsQueue;
}

void CmdDrawInternal(VulkanCmdListData &cmdData)
{
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    const std::array<uint32_t, 4> offsets{
        cmdData.frameConstantHead,
        cmdData.passConstantHead,
        cmdData.drawConstantHead,
        cmdData.largeDrawConstantHead,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 0, 1,
                            &m_ConstantsDescriptorTable.set, offsets.size(), offsets.data());

    cmdData.drawConstantHead += CbvOffset(m_Limits.drawConstantSize);
    cmdData.largeDrawConstantHead += CbvOffset(m_Limits.largeDrawConstantSize);
}

void CreateSwapchain()
{
    VkSwapchainKHR oldSwapchain = m_Swapchain;

    static bool logPresentModes = true;
    VkPresentModeKHR presentMode = [] -> VkPresentModeKHR {
        std::vector<VkPresentModeKHR> presentModes;
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDev, m_Surface, &presentModeCount, nullptr);

        presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDev, m_Surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR presentMode : presentModes)
        {
            if (logPresentModes)
                NYLA_LOG("Present Mode available: %s", string_VkPresentModeKHR(presentMode));

            bool better;
            switch (presentMode)
            {

            case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
                better = !Any(m_Flags & RhiFlags::VSync);
                break;
            }
            case VK_PRESENT_MODE_IMMEDIATE_KHR: {
                better = !Any(m_Flags & RhiFlags::VSync) && bestMode != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
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
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysDev, m_Surface, &surfaceCapabilities));

    auto surfaceFormat = [] -> VkSurfaceFormatKHR {
        uint32_t surfaceFormatCount;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDev, m_Surface, &surfaceFormatCount, nullptr));

        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDev, m_Surface, &surfaceFormatCount, surfaceFormats.data());

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
            .width = std::clamp(windowSize.width, surfaceCapabilities.minImageExtent.width,
                                surfaceCapabilities.maxImageExtent.width),
            .height = std::clamp(windowSize.height, surfaceCapabilities.minImageExtent.height,
                                 surfaceCapabilities.maxImageExtent.height),
        };
    }();

    NYLA_ASSERT(kRhiMaxNumSwapchainTextures >= surfaceCapabilities.minImageCount);
    uint32_t swapchainMinImageCount = std::min(kRhiMaxNumSwapchainTextures, surfaceCapabilities.minImageCount + 1);
    if (surfaceCapabilities.maxImageCount)
        swapchainMinImageCount = std::min(surfaceCapabilities.maxImageCount, swapchainMinImageCount);

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_Surface,
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
        .oldSwapchain = m_Swapchain,
    };
    VK_CHECK(vkCreateSwapchainKHR(m_Dev, &createInfo, nullptr, &m_Swapchain));

    uint32_t swapchainTexturesCount;
    vkGetSwapchainImagesKHR(m_Dev, m_Swapchain, &swapchainTexturesCount, nullptr);

    NYLA_ASSERT(swapchainTexturesCount <= kRhiMaxNumSwapchainTextures);
    std::array<VkImage, kRhiMaxNumSwapchainTextures> swapchainImages;

    vkGetSwapchainImagesKHR(m_Dev, m_Swapchain, &swapchainTexturesCount, swapchainImages.data());

    for (size_t i = 0; i < swapchainMinImageCount; ++i)
    {
        RhiTexture texture;
        RhiRenderTargetView rtv;

        if (m_SwapchainRTVs.size() > i)
        {
            rtv = m_SwapchainRTVs[i];
            VulkanTextureViewData &rtvData = m_RenderTargetViews.ResolveData(rtv);
            rtvData.format = surfaceFormat.format;

            texture = rtvData.texture;
            VulkanTextureData &textureData = m_Textures.ResolveData(texture);
            textureData.image = swapchainImages[i];
            textureData.state = RhiTextureState::Present;
            textureData.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            textureData.format = surfaceFormat.format;
            textureData.extent = VkExtent3D{surfaceExtent.width, surfaceExtent.height, 1};
            textureData.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vkDestroyImageView(m_Dev, rtvData.imageView, m_Alloc);

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
            vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &rtvData.imageView);
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

            texture = m_Textures.Acquire(textureData);

            rtv = Rhi::CreateRenderTargetView(RhiRenderTargetViewDesc{
                .texture = texture,
            });

            m_SwapchainRTVs.emplace_back(rtv);
        }
    }

    if (oldSwapchain)
        vkDestroySwapchainKHR(m_Dev, oldSwapchain, m_Alloc);
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
    vkCreateSemaphore(m_Dev, &semaphoreCreateInfo, nullptr, &semaphore);
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

    VK_CHECK(vkWaitSemaphores(m_Dev, &waitInfo, 1e9));

#if 0
    {
        uint64_t currentValue = -1;
        vkGetSemaphoreCounterValue(m_Dev, timeline, &currentValue);
        DebugBreak();

        VkSemaphoreSignalInfo info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = timeline,
            .value = waitValue,
        };
        VK_CHECK(vkSignalSemaphore(m_Dev, &info));

        vkGetSemaphoreCounterValue(m_Dev, m_GraphicsQueue.timeline, &currentValue);
        DebugBreak();
    }
#endif
}

void WriteDescriptorTables()
{
    constexpr uint32_t kMaxDescriptorUpdates = 128;

    InlineVec<VkWriteDescriptorSet, kMaxDescriptorUpdates> descriptorWrites;
    InlineVec<VkDescriptorImageInfo, kMaxDescriptorUpdates> descriptorImageInfos;
    InlineVec<VkDescriptorBufferInfo, kMaxDescriptorUpdates> descriptorBufferInfos;

    { // TEXTURES
        for (uint32_t i = 0; i < m_SampledTextureViews.size(); ++i)
        {
            auto &slot = m_SampledTextureViews[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanTextureViewData &textureViewData = slot.data;
            const VulkanTextureData &textureData = m_Textures.ResolveData(textureViewData.texture);

            VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.emplace_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_TexturesDescriptorTable.set,
                .dstBinding = 0,
                .dstArrayElement = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            });

            vulkanSetWrite.pImageInfo = &descriptorImageInfos.emplace_back(VkDescriptorImageInfo{
                .imageView = textureViewData.imageView,
                .imageLayout = textureData.layout,
            });

            slot.data.descriptorWritten = true;
        }
    }

    { // SAMPLERS
        for (uint32_t i = 0; i < m_Samplers.size(); ++i)
        {
            auto &slot = m_Samplers[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanSamplerData &samplerData = slot.data;

            VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.emplace_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_SamplersDescriptorTable.set,
                .dstBinding = 0,
                .dstArrayElement = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            });

            vulkanSetWrite.pImageInfo = &descriptorImageInfos.emplace_back(VkDescriptorImageInfo{
                .sampler = samplerData.sampler,
            });

            slot.data.descriptorWritten = true;
        }
    }

    vkUpdateDescriptorSets(m_Dev, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

} // namespace

void Rhi::Init(const RhiInitDesc &rhiDesc)
{
    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    NYLA_ASSERT(rhiDesc.limits.numFramesInFlight <= kRhiMaxNumFramesInFlight);

    m_Flags = rhiDesc.flags;
    m_Limits = rhiDesc.limits;

    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "nyla",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "nyla",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    InlineVec<const char *, 4> enabledInstanceExtensions;
    enabledInstanceExtensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__linux__)
    enabledInstanceExtensions.emplace_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#else
    enabledInstanceExtensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    InlineVec<const char *, 2> instanceLayers;
    if (kRhiValidations)
    {
        instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
    }

    InlineVec<VkValidationFeatureEnableEXT, 4> validationEnabledFeatures;
    InlineVec<VkValidationFeatureDisableEXT, 4> validationDisabledFeatures;
    VkValidationFeaturesEXT validationFeatures = {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 0,
        .pEnabledValidationFeatures = validationEnabledFeatures.data(),
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures = validationDisabledFeatures.data(),
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

    std::vector<VkLayerProperties> layers;
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    layers.resize(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    {
        std::vector<VkExtensionProperties> instanceExtensions;
        uint32_t instanceExtensionsCount;
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, nullptr);

        instanceExtensions.resize(instanceExtensionsCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, instanceExtensions.data());

        NYLA_LOG("");
        NYLA_LOG("%d Instance Extensions available", instanceExtensionsCount);
        for (uint32_t i = 0; i < instanceExtensionsCount; ++i)
        {
            const auto &extension = instanceExtensions[i];
            NYLA_LOG("%s", extension.extensionName);
        }
    }

    NYLA_LOG("");
    NYLA_LOG("%zd Layers available", layers.size());
    for (uint32_t i = 0; i < layerCount; ++i)
    {
        const auto &layer = layers[i];
        NYLA_LOG("    %s", layer.layerName);

        std::vector<VkExtensionProperties> layerExtensions;
        uint32_t layerExtensionProperties;
        vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, nullptr);

        layerExtensions.resize(layerExtensionProperties);
        vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, layerExtensions.data());

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
            validationEnabledFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
            validationEnabledFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            validationEnabledFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
            validationEnabledFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
            validationFeatures.enabledValidationFeatureCount = validationEnabledFeatures.size();

            validationDisabledFeatures.emplace_back(VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT);
            validationFeatures.disabledValidationFeatureCount = validationEnabledFeatures.size();
        }

        instancePNext = &debugMessengerCreateInfo;
        enabledInstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        enabledInstanceExtensions.emplace_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }

    const VkInstanceCreateInfo instanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = instancePNext,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(instanceLayers.size()),
        .ppEnabledLayerNames = instanceLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabledInstanceExtensions.size()),
        .ppEnabledExtensionNames = enabledInstanceExtensions.data(),
    };
    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &m_Instance));

    if constexpr (kRhiValidations)
    {
        auto createDebugUtilsMessenger = VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT);
        VK_CHECK(createDebugUtilsMessenger(m_Instance, &debugMessengerCreateInfo, nullptr, &debugMessenger));
    }

    uint32_t numPhysDevices = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_Instance, &numPhysDevices, nullptr));

    std::vector<VkPhysicalDevice> physDevs(numPhysDevices);
    VK_CHECK(vkEnumeratePhysicalDevices(m_Instance, &numPhysDevices, physDevs.data()));

    std::vector<const char *> deviceExtensions;
    deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
    deviceExtensions.emplace_back(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);

    if constexpr (kRhiCheckpoints)
    {
        deviceExtensions.emplace_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    }

    for (VkPhysicalDevice physDev : physDevs)
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, extensions.data());

        uint32_t missingExtensions = deviceExtensions.size();
        for (auto &deviceExtension : deviceExtensions)
        {
            for (uint32_t i = 0; i < extensionCount; ++i)
            {
                if (strcmp(extensions[i].extensionName, deviceExtension) == 0)
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
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyPropCount, queueFamilyProperties.data());

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
        m_PhysDev = physDev;
        m_PhysDevProps = props;
        m_PhysDevMemProps = memProps;
        m_GraphicsQueue.queueFamilyIndex = graphicsQueueIndex;
        m_TransferQueue.queueFamilyIndex = transferQueueIndex;

        break;
    }

    NYLA_ASSERT(m_PhysDev);

    const float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    if (m_TransferQueue.queueFamilyIndex == kInvalidIndex)
    {
        queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_GraphicsQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }
    else
    {
        queueCreateInfos.reserve(2);

        queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_GraphicsQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });

        queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_TransferQueue.queueFamilyIndex,
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
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
    };
    VK_CHECK(vkCreateDevice(m_PhysDev, &deviceCreateInfo, nullptr, &m_Dev));

    vkGetDeviceQueue(m_Dev, m_GraphicsQueue.queueFamilyIndex, 0, &m_GraphicsQueue.queue);

    if (m_TransferQueue.queueFamilyIndex == kInvalidIndex)
    {
        m_TransferQueue.queueFamilyIndex = m_GraphicsQueue.queueFamilyIndex;
        m_TransferQueue.queue = m_GraphicsQueue.queue;
    }
    else
    {
        vkGetDeviceQueue(m_Dev, m_TransferQueue.queueFamilyIndex, 0, &m_TransferQueue.queue);
    }

    auto initQueue = [](DeviceQueue &queue, RhiQueueType queueType, std::span<RhiCmdList> cmd) -> void {
        const VkCommandPoolCreateInfo commandPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue.queueFamilyIndex,
        };
        VK_CHECK(vkCreateCommandPool(m_Dev, &commandPoolCreateInfo, nullptr, &queue.cmdPool));

        queue.timeline = CreateTimeline(0);
        queue.timelineNext = 1;

        for (auto &i : cmd)
        {
            i = CreateCmdList(queueType);
        }
    };
    initQueue(m_GraphicsQueue, RhiQueueType::Graphics,
              std::span{m_GraphicsQueueCmd.data(), m_Limits.numFramesInFlight});
    initQueue(m_TransferQueue, RhiQueueType::Transfer, std::span{&m_TransferQueueCmd, 1});

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (size_t i = 0; i < m_Limits.numFramesInFlight; ++i)
    {
        VK_CHECK(vkCreateSemaphore(m_Dev, &semaphoreCreateInfo, nullptr, m_SwapchainAcquireSemaphores.data() + i));
    }
    for (size_t i = 0; i < kRhiMaxNumSwapchainTextures; ++i)
    {
        VK_CHECK(vkCreateSemaphore(m_Dev, &semaphoreCreateInfo, nullptr, m_RenderFinishedSemaphores.data() + i));
    }

#if defined(__linux__)
    const VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = xcb_connect(nullptr, nullptr),
        .window = LinuxX11Platform::WinGetHandle(),
    };
    VK_CHECK(vkCreateXcbSurfaceKHR(m_Instance, &surfaceCreateInfo, m_Alloc, &m_Surface));
#else
    const VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = WindowsPlatform::GetHInstance(),
        .hwnd = WindowsPlatform::WinGetHandle(),
    };
    vkCreateWin32SurfaceKHR(m_Instance, &surfaceCreateInfo, m_Alloc, &m_Surface);
#endif

    CreateSwapchain();

    //

    const std::array<VkDescriptorPoolSize, 4> descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 8},
    };

    const VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 16,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
        .pPoolSizes = descriptorPoolSizes.data(),
    };
    vkCreateDescriptorPool(m_Dev, &descriptorPoolCreateInfo, nullptr, &m_DescriptorPool);

    auto initDescriptorTable = [](DescriptorTable &table,
                                  const VkDescriptorSetLayoutCreateInfo &layoutCreateInfo) -> void {
        VK_CHECK(vkCreateDescriptorSetLayout(m_Dev, &layoutCreateInfo, m_Alloc, &table.layout));

        const VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &table.layout,
        };

        VK_CHECK(vkAllocateDescriptorSets(m_Dev, &descriptorSetAllocInfo, &table.set));
    };

    { // Constants

        const std::array<VkDescriptorSetLayoutBinding, 4> descriptorLayoutBindings{
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
            .bindingCount = descriptorLayoutBindings.size(),
            .pBindings = descriptorLayoutBindings.data(),
        };

        initDescriptorTable(m_ConstantsDescriptorTable, descriptorSetLayoutCreateInfo);

        const uint32_t bufferSize =
            m_Limits.numFramesInFlight *
            (CbvOffset(m_Limits.frameConstantSize) + m_Limits.maxPassCount * CbvOffset(m_Limits.passConstantSize) +
             m_Limits.maxDrawCount * CbvOffset(m_Limits.drawConstantSize) +
             m_Limits.maxDrawCount * CbvOffset(m_Limits.largeDrawConstantSize));
        NYLA_LOG("Constants Buffer Size: %fmb", (double)bufferSize / 1024.0 / 1024.0);

        m_ConstantsUniformBuffer = CreateBuffer(RhiBufferDesc{
            .size = bufferSize,
            .bufferUsage = RhiBufferUsage::Uniform,
            .memoryUsage = RhiMemoryUsage::CpuToGpu,
        });
        const VkBuffer &vulkanBuffer = m_Buffers.ResolveData(m_ConstantsUniformBuffer).buffer;

        const std::array<VkDescriptorBufferInfo, 4> bufferInfos{
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(m_Limits.frameConstantSize),
            },
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(m_Limits.passConstantSize),
            },
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(m_Limits.drawConstantSize),
            },
            VkDescriptorBufferInfo{
                .buffer = vulkanBuffer,
                .range = CbvOffset(m_Limits.largeDrawConstantSize),
            },
        };

        InlineVec<VkWriteDescriptorSet, bufferInfos.size()> descriptorWrites;
        for (uint32_t i = 0; i < bufferInfos.size(); ++i)
        {
            const VkDescriptorBufferInfo &bufferInfo = bufferInfos[i];
            if (bufferInfo.range)
            {
                descriptorWrites.emplace_back(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_ConstantsDescriptorTable.set,
                    .dstBinding = i,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .pBufferInfo = &bufferInfo,
                });
            }
        }

        vkUpdateDescriptorSets(m_Dev, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
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
            .descriptorCount = m_Limits.numTextureViews,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsCreateInfo,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &descriptorLayoutBinding,
        };

        initDescriptorTable(m_TexturesDescriptorTable, descriptorSetLayoutCreateInfo);
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
            .descriptorCount = m_Limits.numSamplers,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &bindingFlagsCreateInfo,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings = &descriptorLayoutBinding,
        };

        initDescriptorTable(m_SamplersDescriptorTable, descriptorSetLayoutCreateInfo);
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
    VK_CHECK(vkCreateBuffer(m_Dev, &bufferCreateInfo, nullptr, &bufferData.buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Dev, bufferData.buffer, &memRequirements);

    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(memRequirements, ConvertMemoryUsageIntoVkMemoryPropertyFlags(desc.memoryUsage));
    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    VK_CHECK(vkAllocateMemory(m_Dev, &memoryAllocInfo, nullptr, &bufferData.memory));
    VK_CHECK(vkBindBufferMemory(m_Dev, bufferData.buffer, bufferData.memory, 0));

    return m_Buffers.Acquire(bufferData);
}

void Rhi::NameBuffer(RhiBuffer buf, std::string_view name)
{
    const VulkanBufferData &bufferData = m_Buffers.ResolveData(buf);
    VulkanNameHandle(VK_OBJECT_TYPE_BUFFER, (uint64_t)bufferData.buffer, name);
}

void Rhi::DestroyBuffer(RhiBuffer buffer)
{
    VulkanBufferData bufferData = m_Buffers.ReleaseData(buffer);

    if (bufferData.mapped)
    {
        vkUnmapMemory(m_Dev, bufferData.memory);
    }
    vkDestroyBuffer(m_Dev, bufferData.buffer, nullptr);
    vkFreeMemory(m_Dev, bufferData.memory, nullptr);
}

auto Rhi::GetBufferSize(RhiBuffer buffer) -> uint64_t
{
    return m_Buffers.ResolveData(buffer).size;
}

auto Rhi::MapBuffer(RhiBuffer buffer) -> char *
{
    const VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);
    if (!bufferData.mapped)
    {
        vkMapMemory(m_Dev, bufferData.memory, 0, VK_WHOLE_SIZE, 0, (void **)&bufferData.mapped);
    }

    return bufferData.mapped;
}

void Rhi::UnmapBuffer(RhiBuffer buffer)
{
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);
    if (bufferData.mapped)
    {
        vkUnmapMemory(m_Dev, bufferData.memory);
        bufferData.mapped = nullptr;
    }
}

void Rhi::CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                        uint32_t size)
{
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanBufferData &dstBufferData = m_Buffers.ResolveData(dst);
    VulkanBufferData &srcBufferData = m_Buffers.ResolveData(src);

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
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

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
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

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
    VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

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
    return m_PhysDevProps.limits.minUniformBufferOffsetAlignment;
}

auto Rhi::GetOptimalBufferCopyOffsetAlignment() -> uint32_t
{
    return m_PhysDevProps.limits.optimalBufferCopyOffsetAlignment;
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
    VK_CHECK(vkAllocateCommandBuffers(m_Dev, &allocInfo, &commandBuffer));

    VulkanCmdListData cmdData{
        .cmdbuf = commandBuffer,
        .queueType = queueType,
    };

    RhiCmdList cmd = m_CmdLists.Acquire(cmdData);
    return cmd;
}

void Rhi::ResetCmdList(RhiCmdList cmd)
{
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    VK_CHECK(vkResetCommandBuffer(cmdbuf, 0));

    cmdData.frameConstantHead = GetFrameIndex() * (CbvOffset(m_Limits.frameConstantSize) +
                                                   m_Limits.maxPassCount * CbvOffset(m_Limits.passConstantSize) +
                                                   m_Limits.maxDrawCount * CbvOffset(m_Limits.drawConstantSize) +
                                                   m_Limits.maxDrawCount * CbvOffset(m_Limits.largeDrawConstantSize));

    cmdData.passConstantHead = cmdData.frameConstantHead + m_Limits.frameConstantSize;

    cmdData.drawConstantHead =
        cmdData.passConstantHead + (m_Limits.maxPassCount * CbvOffset(m_Limits.passConstantSize));

    cmdData.largeDrawConstantHead =
        cmdData.drawConstantHead + (m_Limits.maxDrawCount * CbvOffset(m_Limits.drawConstantSize));
}

void Rhi::NameCmdList(RhiCmdList cmd, std::string_view name)
{
    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VulkanNameHandle(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmdData.cmdbuf, name);
}

void Rhi::DestroyCmdList(RhiCmdList cmd)
{
    VulkanCmdListData cmdData = m_CmdLists.ReleaseData(cmd);
    VkCommandPool cmdPool = GetDeviceQueue(cmdData.queueType).cmdPool;
    vkFreeCommandBuffers(m_Dev, cmdPool, 1, &cmdData.cmdbuf);
}

auto Rhi::CmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return data;
    }

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);

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
    if (m_RecreateSwapchain)
    {
        vkDeviceWaitIdle(m_Dev);
        CreateSwapchain();
        m_SwapchainUsable = true;
        m_RecreateSwapchain = false;
    }
    else
    {
        WaitTimeline(m_GraphicsQueue.timeline, m_GraphicsQueueCmdDone[m_FrameIndex]);
    }

    if (m_SwapchainUsable)
    {
        const VkResult acquireResult =
            vkAcquireNextImageKHR(m_Dev, m_Swapchain, std::numeric_limits<uint64_t>::max(),
                                  m_SwapchainAcquireSemaphores[m_FrameIndex], VK_NULL_HANDLE, &m_SwapchainTextureIndex);

        switch (acquireResult)
        {
        case VK_ERROR_OUT_OF_DATE_KHR: {
            m_SwapchainUsable = false;
            m_RecreateSwapchain = true;
            break;
        }

        case VK_SUBOPTIMAL_KHR: {
            m_SwapchainUsable = true;
            m_RecreateSwapchain = true;
            break;
        }

        default: {
            VK_CHECK(acquireResult);
            m_RecreateSwapchain = false;
            m_SwapchainUsable = true;
            break;
        }
        }
    }

    if (!m_SwapchainUsable)
    {
        vkDeviceWaitIdle(m_Dev);
    }

    const RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    ResetCmdList(cmd);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmdbuf, &commandBufferBeginInfo));

    return cmd;
}

void Rhi::FrameEnd()
{
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];

    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VK_CHECK(vkEndCommandBuffer(cmdbuf));

    WriteDescriptorTables();

    const std::array<VkPipelineStageFlags, 1> waitStages = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSemaphore acquireSemaphore = m_SwapchainAcquireSemaphores[m_FrameIndex];
    const VkSemaphore renderFinishedSemaphore = m_RenderFinishedSemaphores[m_SwapchainTextureIndex];

    const std::array<VkSemaphore, 2> signalSemaphores{
        m_GraphicsQueue.timeline,
        renderFinishedSemaphore,
    };

    m_GraphicsQueueCmdDone[m_FrameIndex] = m_GraphicsQueue.timelineNext++;

    const VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = signalSemaphores.size(),
        .pSignalSemaphoreValues = &m_GraphicsQueueCmdDone[m_FrameIndex],
    };

    if (m_SwapchainUsable)
    {
        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timelineSubmitInfo,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquireSemaphore,
            .pWaitDstStageMask = waitStages.data(),
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdbuf,
            .signalSemaphoreCount = std::size(signalSemaphores),
            .pSignalSemaphores = signalSemaphores.data(),
        };
        VK_CHECK(vkQueueSubmit(m_GraphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE));

        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &m_Swapchain,
            .pImageIndices = &m_SwapchainTextureIndex,
        };

        const VkResult presentResult = vkQueuePresentKHR(m_GraphicsQueue.queue, &presentInfo);
    }

    m_FrameIndex = (m_FrameIndex + 1) % m_Limits.numFramesInFlight;
}

auto Rhi::FrameGetCmdList() -> RhiCmdList
{ // TODO: get rid of this
    return m_GraphicsQueueCmd[m_FrameIndex];
}

void Rhi::PassBegin(RhiPassDesc desc)
{
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    const VulkanTextureViewData &rtvData = m_RenderTargetViews.ResolveData(desc.rtv);
    const VulkanTextureData &renderTargetData = m_Textures.ResolveData(rtvData.texture);

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
        const VulkanTextureViewData &dsvData = m_DepthStencilViews.ResolveData(desc.dsv);
        const VulkanTextureData &depthStencilData = m_Textures.ResolveData(dsvData.texture);

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
    RhiCmdList cmd = m_GraphicsQueueCmd[m_FrameIndex];
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    vkCmdEndRendering(cmdbuf);

    cmdData.passConstantHead += CbvOffset(m_Limits.passConstantSize);
}

auto Rhi::CreateShader(const RhiShaderDesc &desc) -> RhiShader
{
    const RhiShader handle = m_Shaders.Acquire(VulkanShaderData{.spv = new InlineVec<uint32_t, 1 << 18>{desc.code}});
    VulkanShaderData &shaderData = m_Shaders.ResolveData(handle);

    return handle;
}

void Rhi::DestroyShader(RhiShader shader)
{
    m_Shaders.ReleaseData(shader);
#if 0
    VkShaderModule shaderModule = m_Shaders.ReleaseData(shader);
    vkDestroyShaderModule(m_Dev, shaderModule, nullptr);
#endif
}

void Rhi::DestroyGraphicsPipeline(RhiGraphicsPipeline pipeline)
{
    auto pipelineData = m_GraphicsPipelines.ReleaseData(pipeline);
    vkDeviceWaitIdle(m_Dev);

    if (pipelineData.layout)
    {
        vkDestroyPipelineLayout(m_Dev, pipelineData.layout, nullptr);
    }
    if (pipelineData.pipeline)
    {
        vkDestroyPipeline(m_Dev, pipelineData.pipeline, nullptr);
    }
}

void Rhi::CmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline)
{
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(pipeline);

    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.pipeline);
    cmdData.boundGraphicsPipeline = pipeline;

    std::array<VkDescriptorSet, 2> descriptorSets{
        m_TexturesDescriptorTable.set,
        m_SamplersDescriptorTable.set,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 1, descriptorSets.size(),
                            descriptorSets.data(), 0, nullptr);
}

void Rhi::CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data)
{
    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    vkCmdPushConstants(cmdData.cmdbuf, pipelineData.layout, ConvertShaderStageIntoVkShaderStageFlags(stage), offset,
                       data.size(), data.data());
}

void Rhi::CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                               std::span<const uint64_t> offsets)
{
    NYLA_ASSERT(buffers.size() == offsets.size());
    NYLA_ASSERT(buffers.size() <= 4U);

    std::array<VkBuffer, 4> vkBufs;
    std::array<VkDeviceSize, 4> vkOffsets;
    for (uint32_t i = 0; i < buffers.size(); ++i)
    {
        vkBufs[i] = m_Buffers.ResolveData(buffers[i]).buffer;
        vkOffsets[i] = offsets[i];
    }

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    vkCmdBindVertexBuffers(cmdData.cmdbuf, firstBinding, buffers.size(), vkBufs.data(), vkOffsets.data());
}

void Rhi::CmdBindIndexBuffer(RhiCmdList cmd, RhiBuffer buffer, uint64_t offset)
{
    const VulkanBufferData &bufferData = m_Buffers.ResolveData(buffer);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    vkCmdBindIndexBuffer(cmdData.cmdbuf, bufferData.buffer, offset, VkIndexType::VK_INDEX_TYPE_UINT16);
}

void Rhi::CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                  uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    CmdDrawInternal(cmdData);
    vkCmdDraw(cmdData.cmdbuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Rhi::CmdDrawIndexed(RhiCmdList cmd, uint32_t indexCount, int32_t vertexOffset, uint32_t instanceCount,
                         uint32_t firstIndex, uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
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
    VK_CHECK(vkCreateSampler(m_Dev, &createInfo, m_Alloc, &samplerData.sampler));

    return m_Samplers.Acquire(samplerData);
}

void Rhi::DestroySampler(RhiSampler sampler)
{
    VulkanSamplerData samplerData = m_Samplers.ReleaseData(sampler);
    vkDestroySampler(m_Dev, samplerData.sampler, m_Alloc);
}

auto Rhi::GetBackbufferView() -> RhiRenderTargetView
{
    return m_SwapchainRTVs[m_SwapchainTextureIndex];
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
    VK_CHECK(vkCreateImage(m_Dev, &imageCreateInfo, m_Alloc, &textureData.image));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(m_Dev, textureData.image, &memoryRequirements);

    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = FindMemoryTypeIndex(memoryRequirements, memoryPropertyFlags),
    };
    vkAllocateMemory(m_Dev, &memoryAllocInfo, m_Alloc, &textureData.memory);
    vkBindImageMemory(m_Dev, textureData.image, textureData.memory, 0);

    return m_Textures.Acquire(textureData);
}

auto Rhi::CreateSampledTextureView(const RhiTextureViewDesc &desc) -> RhiSampledTextureView
{
    VulkanTextureData &textureData = m_Textures.ResolveData(desc.texture);

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

    VK_CHECK(vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &textureViewData.imageView));

    const RhiSampledTextureView view = m_SampledTextureViews.Acquire(textureViewData);
    return view;
}

auto Rhi::CreateRenderTargetView(const RhiRenderTargetViewDesc &desc) -> RhiRenderTargetView
{
    VulkanTextureData &textureData = m_Textures.ResolveData(desc.texture);

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

    VK_CHECK(vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &renderTargetViewData.imageView));
    const RhiRenderTargetView rtv = m_RenderTargetViews.Acquire(renderTargetViewData);
    return rtv;
}

auto Rhi::CreateDepthStencilView(const RhiDepthStencilViewDesc &desc) -> RhiDepthStencilView
{
    VulkanTextureData &textureData = m_Textures.ResolveData(desc.texture);

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

    VK_CHECK(vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &dsvData.imageView));
    const RhiDepthStencilView dsv = m_DepthStencilViews.Acquire(dsvData);
    return dsv;
}

auto Rhi::GetTexture(RhiSampledTextureView srv) -> RhiTexture
{
    return m_SampledTextureViews.ResolveData(srv).texture;
}

auto Rhi::GetTexture(RhiRenderTargetView rtv) -> RhiTexture
{
    return m_RenderTargetViews.ResolveData(rtv).texture;
}

auto Rhi::GetTexture(RhiDepthStencilView dsv) -> RhiTexture
{
    return m_DepthStencilViews.ResolveData(dsv).texture;
}

auto Rhi::GetTextureInfo(RhiTexture texture) -> RhiTextureInfo
{
    const VulkanTextureData &textureData = m_Textures.ResolveData(texture);
    return {
        .width = textureData.extent.width,
        .height = textureData.extent.height,
        .format = ConvertVkFormatIntoTextureFormat(textureData.format),
    };
}

void Rhi::CmdTransitionTexture(RhiCmdList cmd, RhiTexture texture, RhiTextureState newState)
{
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &textureData = m_Textures.ResolveData(texture);

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
    VulkanTextureData textureData = m_Textures.ReleaseData(texture);

    NYLA_ASSERT(textureData.image);
    vkDestroyImage(m_Dev, textureData.image, m_Alloc);

    NYLA_ASSERT(textureData.memory);
    vkFreeMemory(m_Dev, textureData.memory, m_Alloc);
}

void Rhi::DestroySampledTextureView(RhiSampledTextureView textureView)
{
    const VulkanTextureViewData &textureViewData = m_SampledTextureViews.ReleaseData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    vkDestroyImageView(m_Dev, textureViewData.imageView, m_Alloc);
}

void Rhi::DestroyRenderTargetView(RhiRenderTargetView textureView)
{
    const VulkanTextureViewData &textureViewData = m_RenderTargetViews.ReleaseData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    vkDestroyImageView(m_Dev, textureViewData.imageView, m_Alloc);
}

void Rhi::DestroyDepthStencilView(RhiDepthStencilView textureView)
{
    const VulkanTextureViewData &textureViewData = m_DepthStencilViews.ReleaseData(textureView);
    NYLA_ASSERT(textureViewData.imageView);

    vkDestroyImageView(m_Dev, textureViewData.imageView, m_Alloc);
}

void Rhi::CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, RhiBuffer src, uint32_t srcOffset, uint32_t size)
{
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &dstTextureData = m_Textures.ResolveData(dst);
    VulkanBufferData &srcBufferData = m_Buffers.ResolveData(src);

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
    const VkCommandBuffer &cmdbuf = m_CmdLists.ResolveData(cmd).cmdbuf;

    VulkanTextureData &dstTextureData = m_Textures.ResolveData(dst);
    VulkanTextureData &srcTextureData = m_Textures.ResolveData(src);

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

    SpirvShaderManager(std::span<uint32_t> view, RhiShaderStage stage) : m_SpvView{view}, m_Stage{stage}
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

    auto FindLocationBySemantic(std::string_view semantic, StorageClass storageClass, uint32_t *outLocation) -> bool
    {
        uint32_t id;
        if (!FindIdBySemantic(semantic, storageClass, &id))
            return false;

        uint32_t location;
        for (uint32_t i = 0; i < m_Locations.size(); ++i)
        {
            if (m_Locations[i].id == id && CheckStorageClass(id, storageClass))
            {
                *outLocation = m_Locations[i].location;
                return true;
            }
        }

        return false;
    }

    auto FindIdBySemantic(std::string_view querySemantic, StorageClass storageClass, uint32_t *outId) -> bool
    {
        for (uint32_t i = 0; i < m_SemanticDataNames.size(); ++i)
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

    auto FindSemanticById(uint32_t id, std::string_view *outSemantic) -> bool
    {
        for (uint32_t i = 0; i < m_SemanticDataIds.size(); ++i)
        {
            if (m_SemanticDataIds[i] == id)
            {
                *outSemantic = m_SemanticDataNames[i].StringView();
                return true;
            }
        }
        return false;
    }

    auto RewriteLocationForSemantic(std::string_view semantic, StorageClass storageClass, uint32_t aLocation) -> bool
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
            for (uint32_t i = 0; i < m_InputVariables.size(); ++i)
                if (id == m_InputVariables[i])
                    return true;
            return false;
        }
        case StorageClass::Output: {
            for (uint32_t i = 0; i < m_OutputVariables.size(); ++i)
                if (id == m_OutputVariables[i])
                    return true;
            return false;
        }
        default: {
            NYLA_ASSERT(false);
        }
        }
    }

    auto CheckStorageClass(std::string_view semantic, StorageClass storageClass) -> bool
    {
        if (uint32_t id; FindIdBySemantic(semantic, storageClass, &id))
            return CheckStorageClass(id, storageClass);
        return false;
    }

    auto GetInputIds() -> std::span<const uint32_t>
    {
        return m_InputVariables;
    }

    auto GetSemantics() -> std::span<InlineString<16>>
    {
        return m_SemanticDataNames;
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

        std::string_view name = operandReader.String();
        if (name == "SPV_GOOGLE_hlsl_functionality1")
            goto nop;
        if (name == "SPV_GOOGLE_user_type")
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
            m_Locations.emplace_back(LocationData{
                .id = targetId,
                .location = location,
            });
            break;
        }

        case spv::Decoration::DecorationBuiltIn: {
            const uint32_t builtin = operandReader.Word();
            m_Builtin.emplace_back(BuiltinData{
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
            m_SemanticDataIds.emplace_back(targetId);

            auto &name = m_SemanticDataNames.emplace_back(operandReader.String());
            name.AsciiToUpper();

            NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(name.StringView()));
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
            m_InputVariables.emplace_back(resultId);
            break;
        case spv::StorageClass::StorageClassOutput:
            m_OutputVariables.emplace_back(resultId);
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
    auto &vertexShaderData = m_Shaders.ResolveData(desc.vs);
    SpirvShaderManager vsMan(vertexShaderData.spv->GetSpan(), RhiShaderStage::Vertex);

    auto &pixelShaderData = m_Shaders.ResolveData(desc.ps);
    SpirvShaderManager psMan(pixelShaderData.spv->GetSpan(), RhiShaderStage::Pixel);

    VulkanPipelineData pipelineData = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };

    InlineVec<VkVertexInputBindingDescription, 16> vertexBindings;
    for (auto &binding : desc.vertexBindings)
    {
        vertexBindings.emplace_back(VkVertexInputBindingDescription{
            .binding = binding.binding,
            .stride = binding.stride,
            .inputRate = ConvertVulkanInputRate(binding.inputRate),
        });
    }

    InlineVec<VkVertexInputAttributeDescription, 16> vertexAttributes;
    for (auto &attribute : desc.vertexAttributes)
    {
        uint32_t location;
        if (!vsMan.FindLocationBySemantic(attribute.semantic.StringView(), SpirvShaderManager::StorageClass::Input,
                                          &location))
            NYLA_ASSERT(false);

        vertexAttributes.emplace_back(VkVertexInputAttributeDescription{
            .location = location,
            .binding = attribute.binding,
            .format = ConvertVertexFormatIntoVkFormat(attribute.format),
            .offset = attribute.offset,
        });
    }

    for (uint32_t id : psMan.GetInputIds())
    {
        std::string_view semantic;
        if (!psMan.FindSemanticById(id, &semantic))
            NYLA_ASSERT(false);

        if (semantic.starts_with("SV_"))
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
        VK_CHECK(vkCreateShaderModule(m_Dev, &createInfo, nullptr, &shaderModule));

        return shaderModule;
    };

    Erase(*vertexShaderData.spv, Spirview::kNop);
    Erase(*pixelShaderData.spv, Spirview::kNop);

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = createShaderModule(vertexShaderData.spv->GetSpan()),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = createShaderModule(pixelShaderData.spv->GetSpan()),
            .pName = "main",
        },
    };

    const VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = vertexBindings.size(),
        .pVertexBindingDescriptions = vertexBindings.data(),
        .vertexAttributeDescriptionCount = vertexAttributes.size(),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
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
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {},
    };

    const auto dynamicStates = std::to_array<VkDynamicState>({
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    });

    const VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data(),
    };

    InlineVec<VkFormat, 16> colorTargetFormats;
    for (RhiTextureFormat colorTargetFormat : desc.colorTargetFormats)
    {
        colorTargetFormats.emplace_back(ConvertTextureFormatIntoVkFormat(colorTargetFormat, nullptr));
    }

    const VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = colorTargetFormats.size(),
        .pColorAttachmentFormats = colorTargetFormats.data(),
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

    const std::array<VkDescriptorSetLayout, 3> descriptorSetLayouts = {
        m_ConstantsDescriptorTable.layout,
        m_TexturesDescriptorTable.layout,
        m_SamplersDescriptorTable.layout,
    };

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = descriptorSetLayouts.size(),
        .pSetLayouts = descriptorSetLayouts.data(),
#if 0
        .pushConstantRangeCount = desc.pushConstantSize > 0,
        .pPushConstantRanges = &pushConstantRange,
#endif
    };

    vkCreatePipelineLayout(m_Dev, &pipelineLayoutCreateInfo, nullptr, &pipelineData.layout);

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
        .stageCount = stages.size(),
        .pStages = stages.data(),
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

    VK_CHECK(vkCreateGraphicsPipelines(m_Dev, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipelineData.pipeline));

    return m_GraphicsPipelines.Acquire(pipelineData);
}

void Rhi::NameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name)
{
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(pipeline);
    VulkanNameHandle(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelineData.pipeline, name);
}

void Rhi::SetFrameConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= m_Limits.frameConstantSize);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(m_ConstantsUniformBuffer);
    memcpy(mem + cmdData.frameConstantHead, data.data(), data.size());
}

void Rhi::SetPassConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= m_Limits.passConstantSize);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(m_ConstantsUniformBuffer);
    memcpy(mem + cmdData.passConstantHead, data.data(), data.size());
}

void Rhi::SetDrawConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= m_Limits.drawConstantSize);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(m_ConstantsUniformBuffer);
    memcpy(mem + cmdData.drawConstantHead, data.data(), data.size());
}

void Rhi::SetLargeDrawConstant(RhiCmdList cmd, std::span<const std::byte> data)
{
    NYLA_ASSERT(data.size() <= m_Limits.largeDrawConstantSize);

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);

    char *mem = MapBuffer(m_ConstantsUniformBuffer);
    memcpy(mem + cmdData.largeDrawConstantHead, data.data(), data.size());

    if constexpr (false)
    {
        const VulkanBufferData &bufferData = m_Buffers.ResolveData(m_ConstantsUniformBuffer);
        const VkMappedMemoryRange range{
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = bufferData.memory,
        };
        vkInvalidateMappedMemoryRanges(m_Dev, 1, &range);
    }
}

void Rhi::TriggerSwapchainRecreate()
{
    m_RecreateSwapchain = true;
}

auto Rhi::GetFrameIndex() -> uint32_t
{
    return m_FrameIndex;
}

auto Rhi::GetNumFramesInFlight() -> uint32_t
{
    return m_Limits.numFramesInFlight;
}

} // namespace nyla

#undef VK_GET_INSTANCE_PROC_ADDR
#undef VK_CHECK
