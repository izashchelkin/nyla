#include "nyla/commons/platform.h"
#include "nyla/commons/rhi.h"

#include <cstdint>

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "nyla/commons/array.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/collections.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_utils.h"
#include "nyla/commons/span.h"
#include "nyla/commons/spv_shader.h"

// clang-format off
#ifdef __linux__
#include "nyla/commons/platform_linux.h"
#include "vulkan/vulkan_xcb.h"
#else
#include "nyla/commons/platform_windows.h"
#include "vulkan/vulkan_win32.h"
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
    rhi_memory_usage memoryUsage;
    VkDeviceMemory memory;
    char *mapped;
    rhi_buffer_state state;

    uint32_t dirtyBegin;
    uint32_t dirtyEnd;
    bool dirty;
};

struct VulkanBufferViewData
{
    rhi_buffer buffer;
    uint32_t size;
    uint32_t offset;
    uint32_t range;
    bool dynamic;

    bool descriptorWritten;
};

struct VulkanCmdListData
{
    VkCommandBuffer cmdbuf;
    rhi_queue_type queueType;
    rhi_graphics_pipeline boundGraphicsPipeline;

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
    span<uint32_t> spv;
};

struct VulkanTextureData
{
    VkImage image;
    VkDeviceMemory memory;
    rhi_texture_state state;
    VkImageLayout layout;
    VkFormat format;
    VkImageAspectFlags aspectMask;
    VkExtent3D extent;
};

struct VulkanTextureViewData
{
    rhi_texture texture;

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

auto ConvertBufferUsageIntoVkBufferUsageFlags(rhi_buffer_usage usage) -> VkBufferUsageFlags
{
    VkBufferUsageFlags ret = 0;

    if (Any(usage & rhi_buffer_usage::Vertex))
    {
        ret |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (Any(usage & rhi_buffer_usage::Index))
    {
        ret |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (Any(usage & rhi_buffer_usage::Uniform))
    {
        ret |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (Any(usage & rhi_buffer_usage::CopySrc))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & rhi_buffer_usage::CopyDst))
    {
        ret |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return ret;
}

auto ConvertMemoryUsageIntoVkMemoryPropertyFlags(rhi_memory_usage usage) -> VkMemoryPropertyFlags
{
    // TODO: not all GPUs support HOST_COHERENT, HOST_CACHED

    switch (usage)
    {
    case rhi_memory_usage::Unknown:
        break;
    case rhi_memory_usage::GpuOnly:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case rhi_memory_usage::CpuToGpu:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case rhi_memory_usage::GpuToCpu:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
               VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    ASSERT(false);
    return 0;
}

auto ConvertVertexFormatIntoVkFormat(rhi_vertex_format format) -> VkFormat
{
    switch (format)
    {
    case rhi_vertex_format::None:
        break;
    case rhi_vertex_format::R32G32B32A32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case rhi_vertex_format::R32G32B32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case rhi_vertex_format::R32G32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    }
    ASSERT(false);
    return static_cast<VkFormat>(0);
}

auto ConvertShaderStageIntoVkShaderStageFlags(rhi_shader_stage stageFlags) -> VkShaderStageFlags
{
    VkShaderStageFlags ret = 0;
    if (Any(stageFlags & rhi_shader_stage::Vertex))
    {
        ret |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (Any(stageFlags & rhi_shader_stage::Pixel))
    {
        ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return ret;
}

auto ConvertVulkanCullMode(rhi_cull_mode cullMode) -> VkCullModeFlags
{
    switch (cullMode)
    {
    case rhi_cull_mode::None:
        return VK_CULL_MODE_NONE;

    case rhi_cull_mode::Back:
        return VK_CULL_MODE_BACK_BIT;

    case rhi_cull_mode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    }
    ASSERT(false);
    return static_cast<VkCullModeFlags>(0);
}

auto ConvertVulkanFrontFace(rhi_front_face frontFace) -> VkFrontFace
{
    switch (frontFace)
    {
    case rhi_front_face::CCW:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;

    case rhi_front_face::CW:
        return VK_FRONT_FACE_CLOCKWISE;
    }
    ASSERT(false);
    return static_cast<VkFrontFace>(0);
}

auto ConvertFilter(rhi_filter filter) -> VkFilter
{
    switch (filter)
    {
    case rhi_filter::Linear:
        return VK_FILTER_LINEAR;
    case rhi_filter::Nearest:
        return VK_FILTER_NEAREST;
    }
    ASSERT(false);
    return {};
}

auto ConvertSamplerAddressMode(rhi_sampler_address_mode addressMode) -> VkSamplerAddressMode
{
    switch (addressMode)
    {
    case rhi_sampler_address_mode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case rhi_sampler_address_mode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    ASSERT(false);
    return {};
}

auto ConvertVulkanInputRate(rhi_input_rate inputRate) -> VkVertexInputRate
{
    switch (inputRate)
    {
    case rhi_input_rate::PerInstance:
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    case rhi_input_rate::PerVertex:
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }
    ASSERT(false);
    return static_cast<VkVertexInputRate>(0);
}

struct VulkanTextureStateSyncInfo
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

auto VulkanTextureStateGetSyncInfo(rhi_texture_state state) -> VulkanTextureStateSyncInfo
{
    switch (state)
    {
    case rhi_texture_state::Undefined:
        return {
            .stage = VK_PIPELINE_STAGE_2_NONE,
            .access = VK_ACCESS_2_NONE,
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

    case rhi_texture_state::ColorTarget:
        return {
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

    case rhi_texture_state::DepthTarget:
        return {
            .stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

    case rhi_texture_state::ShaderRead:
        return {
            .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

    case rhi_texture_state::Present:
        return {
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access = VK_ACCESS_2_NONE,
            .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

    case rhi_texture_state::TransferSrc:
        return {
            .stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .access = VK_ACCESS_2_TRANSFER_READ_BIT,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        };

    case rhi_texture_state::TransferDst:
        return {
            .stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        };
    }
    ASSERT(false);
    return {};
}

auto ConvertTextureFormatIntoVkFormat(rhi_texture_format format, VkImageAspectFlags *outAspectMask) -> VkFormat
{
    switch (format)
    {
    case rhi_texture_format::None: {
        if (outAspectMask)
            *outAspectMask = 0;
        return VK_FORMAT_UNDEFINED;
    }
    case rhi_texture_format::R8_UNORM: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        return VK_FORMAT_R8_UNORM;
    }
    case rhi_texture_format::R8G8B8A8_sRGB: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        return VK_FORMAT_R8G8B8A8_SRGB;
    }
    case rhi_texture_format::B8G8R8A8_sRGB: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        return VK_FORMAT_B8G8R8A8_SRGB;
    }
    case rhi_texture_format::D32_Float: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        return VK_FORMAT_D32_SFLOAT;
    }
    case rhi_texture_format::D32_Float_S8_UINT: {
        if (outAspectMask)
            *outAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    }
    ASSERT(false);
    return static_cast<VkFormat>(0);
}

auto ConvertVkFormatIntoTextureFormat(VkFormat format) -> rhi_texture_format
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_SRGB:
        return rhi_texture_format::R8G8B8A8_sRGB;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return rhi_texture_format::B8G8R8A8_sRGB;
    case VK_FORMAT_D32_SFLOAT:
        return rhi_texture_format::D32_Float;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return rhi_texture_format::D32_Float_S8_UINT;
    default:
        break;
    }
    ASSERT(false);
    return static_cast<rhi_texture_format>(0);
}

auto ConvertTextureUsageToVkImageUsageFlags(rhi_texture_usage usage) -> VkImageUsageFlags
{
    VkImageUsageFlags flags = 0;

    if (Any(usage & rhi_texture_usage::ShaderSampled))
    {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (Any(usage & rhi_texture_usage::ColorTarget))
    {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (Any(usage & rhi_texture_usage::DepthStencil))
    {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (Any(usage & rhi_texture_usage::TransferSrc))
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (Any(usage & rhi_texture_usage::TransferDst))
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (Any(usage & rhi_texture_usage::Storage))
    {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (Any(usage & rhi_texture_usage::Present))
    {
    }

    return flags;
}

auto GetVertexFormatSize(rhi_vertex_format format) -> uint32_t
{
    switch (format)
    {
    case rhi_vertex_format::None:
        break;
    case rhi_vertex_format::R32G32Float:
        return 8;
    case rhi_vertex_format::R32G32B32Float:
        return 12;
    case rhi_vertex_format::R32G32B32A32Float:
        return 16;
    }
    ASSERT(false);
    return 0;
}

auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                            const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *pUserData) -> VkBool32
{
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
        LOG("%s", callbackData->pMessage);
        TRAP();
        break;
    }
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
        LOG("%s", callbackData->pMessage);
        break;
    }
    default: {
        LOG("%s", callbackData->pMessage);
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
        // LOG("Last checkpoint: %I64d", RhiGetLastCheckpointData(rhi_queue_type::Graphics));
        // FALLTHROUGH

    default: {
        LOG("Vulkan error: %s", string_VkResult(res));
        ASSERT(false);
    }
    }
};

//

struct DescriptorTable
{
    VkDescriptorSetLayout layout;
    VkDescriptorSet set;
};

struct rhi_state
{
    handle_pool<rhi_buffer, VulkanBufferData, 16> m_Buffers;
    handle_pool<rhi_buffer, VulkanBufferViewData, 16> m_CBVs;
    handle_pool<rhi_cmdlist, VulkanCmdListData, 16> m_CmdLists;
    handle_pool<rhi_dsv, VulkanTextureViewData, 8> m_DepthStencilViews;
    handle_pool<rhi_graphics_pipeline, VulkanPipelineData, 16> m_GraphicsPipelines;
    handle_pool<rhi_rtv, VulkanTextureViewData, 8> m_RenderTargetViews;
    handle_pool<rhi_stv, VulkanTextureViewData, 128> m_SampledTextureViews;
    handle_pool<rhi_sampler, VulkanSamplerData, 16> m_Samplers;
    handle_pool<rhi_shader, VulkanShaderData, 16> m_Shaders;
    handle_pool<rhi_texture, VulkanTextureData, 128> m_Textures;

    VkAllocationCallbacks *vkAlloc;

    rhi_flags m_Flags;
    rhi_limits m_Limits;

    VkInstance m_Instance;
    VkDevice m_Dev;
    VkPhysicalDevice m_PhysDev;
    VkPhysicalDeviceProperties m_PhysDevProps;
    VkPhysicalDeviceMemoryProperties m_PhysDevMemProps;
    VkDescriptorPool m_DescriptorPool;

    DescriptorTable m_ConstantsDescriptorTable;
    rhi_buffer m_ConstantsUniformBuffer;
    DescriptorTable m_TexturesDescriptorTable;
    DescriptorTable m_SamplersDescriptorTable;

    VkSurfaceKHR m_Surface;
    VkSwapchainKHR m_Swapchain;
    bool m_SwapchainUsable;
    bool m_RecreateSwapchain;

    inline_vec<rhi_rtv, kRhiMaxNumSwapchainTextures> m_SwapchainRTVs{};
    uint32_t m_SwapchainTextureIndex;

    array<VkSemaphore, kRhiMaxNumSwapchainTextures> m_RenderFinishedSemaphores;
    array<VkSemaphore, kRhiMaxNumFramesInFlight> m_SwapchainAcquireSemaphores;

    DeviceQueue m_GraphicsQueue;
    uint32_t m_FrameIndex;
    array<rhi_cmdlist, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmd;
    array<uint64_t, kRhiMaxNumFramesInFlight> m_GraphicsQueueCmdDone;

    DeviceQueue m_TransferQueue;
    rhi_cmdlist m_TransferQueueCmd;
    uint64_t m_TransferQueueCmdDone;
};
rhi_state *g_State;
region_alloc g_Alloc;

//

void VulkanNameHandle(VkObjectType type, uint64_t handle, byteview name)
{
    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = (const char *)name.data,
    };

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkSetDebugUtilsObjectNameEXT);
    fn(g_State->m_Dev, &nameInfo);
}

auto CbvOffset(uint32_t offset) -> uint32_t
{
    return AlignedUp(offset, g_State->m_PhysDevProps.limits.minUniformBufferOffsetAlignment);
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

    ASSERT(false);
    return 0;
}

void EnsureHostWritesVisible(VkCommandBuffer cmdbuf, VulkanBufferData &bufferData)
{
    if (bufferData.memoryUsage != rhi_memory_usage::CpuToGpu)
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

auto VulkanBufferStateGetSyncInfo(rhi_buffer_state state) -> VulkanBufferStateSyncInfo
{
    switch (state)
    {
    case rhi_buffer_state::Undefined: {
        return {.stage = VK_PIPELINE_STAGE_2_NONE, .access = VK_ACCESS_2_NONE};
    }
    case rhi_buffer_state::CopySrc: {
        return {.stage = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_READ_BIT};
    }
    case rhi_buffer_state::CopyDst: {
        return {.stage = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_WRITE_BIT};
    }
    case rhi_buffer_state::ShaderRead: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    }
    case rhi_buffer_state::ShaderWrite: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
    }
    case rhi_buffer_state::Vertex: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    }
    case rhi_buffer_state::Index: {
        return {.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, .access = VK_ACCESS_2_INDEX_READ_BIT};
    }
    case rhi_buffer_state::Indirect: {
        return {.stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT};
    }
    }
    ASSERT(false);
    return {};
}

auto GetDeviceQueue(rhi_queue_type queueType) -> DeviceQueue &
{
    switch (queueType)
    {
    case rhi_queue_type::Graphics:
        return g_State->m_GraphicsQueue;
    case rhi_queue_type::Transfer:
        return g_State->m_TransferQueue;
    }
    ASSERT(false);
    return g_State->m_GraphicsQueue;
}

void CmdDrawInternal(VulkanCmdListData &cmdData)
{
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    const VulkanPipelineData &pipelineData =
        HandlePool::ResolveData(g_State->m_GraphicsPipelines, cmdData.boundGraphicsPipeline);

    const array<uint32_t, 4> offsets{
        cmdData.frameConstantHead,
        cmdData.passConstantHead,
        cmdData.drawConstantHead,
        cmdData.largeDrawConstantHead,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 0, 1,
                            &g_State->m_ConstantsDescriptorTable.set, Array::Size(offsets), offsets.data);

    cmdData.drawConstantHead += CbvOffset(g_State->m_Limits.drawConstantSize);
    cmdData.largeDrawConstantHead += CbvOffset(g_State->m_Limits.largeDrawConstantSize);
}

void CreateSwapchain(region_alloc scratch)
{
    SCRATCH_REMEMBER;

    VkSwapchainKHR oldSwapchain = g_State->m_Swapchain;

    static bool logPresentModes = true;
    VkPresentModeKHR presentMode = [&] -> VkPresentModeKHR {
        auto &presentModes = RegionAlloc::AllocVec<VkPresentModeKHR, 16>(scratch);
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_State->m_PhysDev, g_State->m_Surface, &presentModeCount, nullptr);

        InlineVec::Resize(presentModes, presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_State->m_PhysDev, g_State->m_Surface, &presentModeCount,
                                                  InlineVec::DataPtr(presentModes));

        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR presentMode : presentModes)
        {
            if (logPresentModes)
                LOG("Present Mode available: %s", string_VkPresentModeKHR(presentMode));

            bool better;
            switch (presentMode)
            {

            case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
                better = !Any(g_State->m_Flags & rhi_flags::VSync);
                break;
            }
            case VK_PRESENT_MODE_IMMEDIATE_KHR: {
                better = !Any(g_State->m_Flags & rhi_flags::VSync) && bestMode != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
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
            LOG("Chose %s", string_VkPresentModeKHR(bestMode));
        }

        logPresentModes = false;

        return bestMode;
    }();

    SCRATCH_RESET;

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_State->m_PhysDev, g_State->m_Surface, &surfaceCapabilities));

    auto surfaceFormat = [&] -> VkSurfaceFormatKHR {
        uint32_t surfaceFormatCount;
        VK_CHECK(
            vkGetPhysicalDeviceSurfaceFormatsKHR(g_State->m_PhysDev, g_State->m_Surface, &surfaceFormatCount, nullptr));

        auto &surfaceFormats = RegionAlloc::AllocVec<VkSurfaceFormatKHR, 16>(scratch);
        InlineVec::Resize(surfaceFormats, surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_State->m_PhysDev, g_State->m_Surface, &surfaceFormatCount,
                                             InlineVec::DataPtr(surfaceFormats));

        auto it = std::ranges::find_if(surfaceFormats, [](VkSurfaceFormatKHR surfaceFormat) -> bool {
            return surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        ASSERT(it != surfaceFormats.end());
        return *it;
    }();

    SCRATCH_RESET;

    auto surfaceExtent = [surfaceCapabilities] -> VkExtent2D {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return surfaceCapabilities.currentExtent;

        const PlatformWindowSize windowSize = WinGetSize();
        return VkExtent2D{
            .width = Clamp(windowSize.width, surfaceCapabilities.minImageExtent.width,
                           surfaceCapabilities.maxImageExtent.width),
            .height = Clamp(windowSize.height, surfaceCapabilities.minImageExtent.height,
                            surfaceCapabilities.maxImageExtent.height),
        };
    }();

    ASSERT(kRhiMaxNumSwapchainTextures >= surfaceCapabilities.minImageCount);
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

    ASSERT(swapchainTexturesCount <= kRhiMaxNumSwapchainTextures);
    array<VkImage, kRhiMaxNumSwapchainTextures> swapchainImages;

    vkGetSwapchainImagesKHR(g_State->m_Dev, g_State->m_Swapchain, &swapchainTexturesCount, swapchainImages.data);

    for (size_t i = 0; i < swapchainMinImageCount; ++i)
    {
        rhi_texture texture;
        rhi_rtv rtv;

        if (g_State->m_SwapchainRTVs.size > i)
        {
            rtv = g_State->m_SwapchainRTVs[i];
            VulkanTextureViewData &rtvData = HandlePool::ResolveData(g_State->m_RenderTargetViews, rtv);
            rtvData.format = surfaceFormat.format;

            texture = rtvData.texture;
            VulkanTextureData &textureData = HandlePool::ResolveData(g_State->m_Textures, texture);
            textureData.image = swapchainImages[i];
            textureData.state = rhi_texture_state::Present;
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
                .state = rhi_texture_state::Present,
                .format = surfaceFormat.format,
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .extent = VkExtent3D{surfaceExtent.width, surfaceExtent.height, 1},
            };

            texture = HandlePool::Acquire(g_State->m_Textures, textureData);

            rtv = Rhi::CreateRenderTargetView(rhi_render_target_view_desc{
                .texture = texture,
            });

            InlineVec::Append(g_State->m_SwapchainRTVs, rtv);
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

void WriteDescriptorTables(region_alloc &scratch)
{
    SCRATCH_REMEMBER;

    constexpr uint32_t kMaxDescriptorUpdates = 128;

    auto &descriptorWrites = RegionAlloc::AllocVec<VkWriteDescriptorSet, kMaxDescriptorUpdates>(scratch);
    auto &descriptorImageInfos = RegionAlloc::AllocVec<VkDescriptorImageInfo, kMaxDescriptorUpdates>(scratch);
    auto &descriptorBufferInfos = RegionAlloc::AllocVec<VkDescriptorBufferInfo, kMaxDescriptorUpdates>(scratch);

    { // TEXTURES
        for (uint32_t i = 0; i < HandlePool::Capacity(g_State->m_SampledTextureViews); ++i)
        {
            auto &slot = g_State->m_SampledTextureViews[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanTextureViewData &textureViewData = slot.data;
            const VulkanTextureData &textureData =
                HandlePool::ResolveData(g_State->m_Textures, textureViewData.texture);

            VkWriteDescriptorSet &vulkanSetWrite =
                InlineVec::Append(descriptorWrites, VkWriteDescriptorSet{
                                                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                        .dstSet = g_State->m_TexturesDescriptorTable.set,
                                                        .dstBinding = 0,
                                                        .dstArrayElement = i,
                                                        .descriptorCount = 1,
                                                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                    });

            vulkanSetWrite.pImageInfo =
                &InlineVec::Append(descriptorImageInfos, VkDescriptorImageInfo{
                                                             .imageView = textureViewData.imageView,
                                                             .imageLayout = textureData.layout,
                                                         });

            slot.data.descriptorWritten = true;
        }
    }

    { // SAMPLERS
        for (uint32_t i = 0; i < HandlePool::Capacity(g_State->m_Samplers); ++i)
        {
            auto &slot = g_State->m_Samplers[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanSamplerData &samplerData = slot.data;

            VkWriteDescriptorSet &vulkanSetWrite =
                InlineVec::Append(descriptorWrites, VkWriteDescriptorSet{
                                                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                        .dstSet = g_State->m_SamplersDescriptorTable.set,
                                                        .dstBinding = 0,
                                                        .dstArrayElement = i,
                                                        .descriptorCount = 1,
                                                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                                    });

            vulkanSetWrite.pImageInfo = &InlineVec::Append(descriptorImageInfos, VkDescriptorImageInfo{
                                                                                     .sampler = samplerData.sampler,
                                                                                 });

            slot.data.descriptorWritten = true;
        }
    }

    vkUpdateDescriptorSets(g_State->m_Dev, descriptorWrites.size, InlineVec::DataPtr(descriptorWrites), 0, nullptr);

    SCRATCH_RESET;
}

} // namespace

void Rhi::Init(region_alloc &scratch, const rhi_init_desc &rhiDesc)
{
    void *scratchResetMark = scratch.at;

    {
        g_Alloc = RegionAlloc::Create(1_MiB, 0);
        g_State = &RegionAlloc::Alloc<rhi_state>(g_Alloc);

        constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

        ASSERT(rhiDesc.limits.numFramesInFlight <= kRhiMaxNumFramesInFlight);

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

        auto &enabledInstanceExtensions = RegionAlloc::AllocVec<const char *, 5>(scratch);
        InlineVec::Append(enabledInstanceExtensions, VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__linux__)
        InlineVec::Append(enabledInstanceExtensions, VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#else
        InlineVec::Append(enabledInstanceExtensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

        auto &instanceLayers = RegionAlloc::AllocVec<const char *, 2>(scratch);
        if (kRhiValidations)
        {
            InlineVec::Append(instanceLayers, "VK_LAYER_KHRONOS_validation");
        }

        auto &validationEnabledFeatures = RegionAlloc::AllocVec<VkValidationFeatureEnableEXT, 4>(scratch);
        auto &validationDisabledFeatures = RegionAlloc::AllocVec<VkValidationFeatureDisableEXT, 4>(scratch);
        VkValidationFeaturesEXT validationFeatures = {
            .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .enabledValidationFeatureCount = (uint32_t)validationEnabledFeatures.size,
            .pEnabledValidationFeatures = InlineVec::DataPtr(validationEnabledFeatures),
            .disabledValidationFeatureCount = (uint32_t)validationDisabledFeatures.size,
            .pDisabledValidationFeatures = InlineVec::DataPtr(validationDisabledFeatures),
        };

        VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = &validationFeatures,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = DebugMessengerCallback,
            .pUserData = nullptr,
        };

        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        auto &layers = RegionAlloc::AllocVec<VkLayerProperties, 256>(scratch);
        InlineVec::Resize(layers, layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, InlineVec::DataPtr(layers));

        {
            auto &instanceExtensions = RegionAlloc::AllocVec<VkExtensionProperties, 256>(scratch);
            uint32_t instanceExtensionsCount;
            vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, nullptr);

            InlineVec::Resize(instanceExtensions, instanceExtensionsCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount,
                                                   InlineVec::DataPtr(instanceExtensions));

            LOG("");
            LOG("%d Instance Extensions available", instanceExtensionsCount);
            for (uint32_t i = 0; i < instanceExtensionsCount; ++i)
            {
                const auto &extension = instanceExtensions[i];
                LOG("%s", extension.extensionName);
            }
        }

        LOG("");
        LOG("%zd Layers available", layers.size);
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            const auto &layer = layers[i];
            LOG("    %s", layer.layerName);

            auto &layerExtensions = RegionAlloc::AllocVec<VkExtensionProperties, 256>(scratch);
            uint32_t layerExtensionProperties;
            vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, nullptr);

            InlineVec::Resize(layerExtensions, layerExtensionProperties);
            vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties,
                                                   InlineVec::DataPtr(layerExtensions));

            for (uint32_t i = 0; i < layerExtensionProperties; ++i)
            {
                const auto &extension = layerExtensions[i];
                LOG("        %s", extension.extensionName);
            }
        }

        VkDebugUtilsMessengerEXT debugMessenger{};

        const void *instancePNext = nullptr;
        if constexpr (kRhiValidations)
        {
            if constexpr (false)
            {
                InlineVec::Append(validationEnabledFeatures, VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
                InlineVec::Append(validationEnabledFeatures,
                                  VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
                InlineVec::Append(validationEnabledFeatures, VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
                InlineVec::Append(validationEnabledFeatures,
                                  VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
                validationFeatures.enabledValidationFeatureCount = validationEnabledFeatures.size;

                InlineVec::Append(validationDisabledFeatures, VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT);
                validationFeatures.disabledValidationFeatureCount = validationEnabledFeatures.size;
            }

            instancePNext = &debugMessengerCreateInfo;
            InlineVec::Append(enabledInstanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            InlineVec::Append(enabledInstanceExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }

        const VkInstanceCreateInfo instanceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = instancePNext,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(instanceLayers.size),
            .ppEnabledLayerNames = InlineVec::DataPtr(instanceLayers),
            .enabledExtensionCount = static_cast<uint32_t>(enabledInstanceExtensions.size),
            .ppEnabledExtensionNames = InlineVec::DataPtr(enabledInstanceExtensions),
        };
        VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &g_State->m_Instance));

        if constexpr (kRhiValidations)
        {
            auto createDebugUtilsMessenger = VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT);
            VK_CHECK(
                createDebugUtilsMessenger(g_State->m_Instance, &debugMessengerCreateInfo, nullptr, &debugMessenger));
        }

        uint32_t numPhysDevices = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(g_State->m_Instance, &numPhysDevices, nullptr));

        auto &physDevs = RegionAlloc::AllocVec<VkPhysicalDevice, 16>(scratch);
        InlineVec::Resize(physDevs, numPhysDevices);
        VK_CHECK(vkEnumeratePhysicalDevices(g_State->m_Instance, &numPhysDevices, InlineVec::DataPtr(physDevs)));

        auto &deviceExtensions = RegionAlloc::AllocVec<const char *, 256>(scratch);
        InlineVec::Append(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        InlineVec::Append(deviceExtensions, VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
        InlineVec::Append(deviceExtensions, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);

        if constexpr (kRhiCheckpoints)
        {
            InlineVec::Append(deviceExtensions, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        }

        for (VkPhysicalDevice physDev : physDevs)
        {
            uint32_t extensionCount = 0;
            vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, nullptr);
            auto &extensions = RegionAlloc::AllocVec<VkExtensionProperties, 256>(scratch);
            InlineVec::Resize(extensions, 256);
            vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, InlineVec::DataPtr(extensions));

            uint32_t missingExtensions = deviceExtensions.size;
            for (const char *deviceExtensionCStr : deviceExtensions)
            {
                auto deviceExtension = Span::FromCStr(deviceExtensionCStr, VK_MAX_EXTENSION_NAME_SIZE);

                for (uint32_t i = 0; i < extensionCount; ++i)
                {
                    if (Span::Eq(deviceExtension,
                                 Span::FromCStr(extensions[i].extensionName, VK_MAX_EXTENSION_NAME_SIZE)))
                    {
                        LOG("Found device extension: " SV_FMT, SV_ARG(deviceExtension));
                        --missingExtensions;
                        break;
                    }
                }
            }

            if (missingExtensions)
            {
                LOG("Missing %d extensions", missingExtensions);
                continue;
            }

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physDev, &props);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

            uint32_t queueFamilyPropCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyPropCount, nullptr);
            auto &queueFamilyProperties = RegionAlloc::AllocVec<VkQueueFamilyProperties, 256>(scratch);
            InlineVec::Resize(queueFamilyProperties, queueFamilyPropCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyPropCount,
                                                     InlineVec::DataPtr(queueFamilyProperties));

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

        ASSERT(g_State->m_PhysDev);

        const float queuePriority = 1.0f;
        auto &queueCreateInfos = RegionAlloc::AllocVec<VkDeviceQueueCreateInfo, 2>(scratch);
        if (g_State->m_TransferQueue.queueFamilyIndex == kInvalidIndex)
        {
            InlineVec::Append(queueCreateInfos, VkDeviceQueueCreateInfo{
                                                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                    .queueFamilyIndex = g_State->m_GraphicsQueue.queueFamilyIndex,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &queuePriority,
                                                });
        }
        else
        {
            InlineVec::Append(queueCreateInfos, VkDeviceQueueCreateInfo{
                                                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                    .queueFamilyIndex = g_State->m_GraphicsQueue.queueFamilyIndex,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &queuePriority,
                                                });

            InlineVec::Append(queueCreateInfos, VkDeviceQueueCreateInfo{
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
            .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size),
            .pQueueCreateInfos = InlineVec::DataPtr(queueCreateInfos),
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size),
            .ppEnabledExtensionNames = InlineVec::DataPtr(deviceExtensions),
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
            vkGetDeviceQueue(g_State->m_Dev, g_State->m_TransferQueue.queueFamilyIndex, 0,
                             &g_State->m_TransferQueue.queue);
        }

        auto initQueue = [](DeviceQueue &queue, rhi_queue_type queueType, span<rhi_cmdlist> cmd) -> void {
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
        initQueue(g_State->m_GraphicsQueue, rhi_queue_type::Graphics,
                  span{g_State->m_GraphicsQueueCmd.data, g_State->m_Limits.numFramesInFlight});
        initQueue(g_State->m_TransferQueue, rhi_queue_type::Transfer, span{&g_State->m_TransferQueueCmd, 1});

        const VkSemaphoreCreateInfo semaphoreCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        for (size_t i = 0; i < g_State->m_Limits.numFramesInFlight; ++i)
        {
            VK_CHECK(vkCreateSemaphore(g_State->m_Dev, &semaphoreCreateInfo, nullptr,
                                       g_State->m_SwapchainAcquireSemaphores.data + i));
        }
        for (size_t i = 0; i < kRhiMaxNumSwapchainTextures; ++i)
        {
            VK_CHECK(vkCreateSemaphore(g_State->m_Dev, &semaphoreCreateInfo, nullptr,
                                       g_State->m_RenderFinishedSemaphores.data + i));
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

        RegionAlloc::Reset(scratch, scratchResetMark);
    }

    CreateSwapchain(scratch);

    //

    const array<VkDescriptorPoolSize, 4> descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 8},
    };

    const VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 16,
        .poolSizeCount = (uint32_t)Array::Size(descriptorPoolSizes),
        .pPoolSizes = descriptorPoolSizes.data,
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

        const array<VkDescriptorSetLayoutBinding, 4> descriptorLayoutBindings{
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
            .bindingCount = (uint32_t)Array::Size(descriptorLayoutBindings),
            .pBindings = descriptorLayoutBindings.data,
        };

        initDescriptorTable(g_State->m_ConstantsDescriptorTable, descriptorSetLayoutCreateInfo);

        const uint32_t bufferSize =
            g_State->m_Limits.numFramesInFlight *
            (CbvOffset(g_State->m_Limits.frameConstantSize) +
             g_State->m_Limits.maxPassCount * CbvOffset(g_State->m_Limits.passConstantSize) +
             g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.drawConstantSize) +
             g_State->m_Limits.maxDrawCount * CbvOffset(g_State->m_Limits.largeDrawConstantSize));
        LOG("Constants Buffer Size: %fmb", (double)bufferSize / 1024.0 / 1024.0);

        g_State->m_ConstantsUniformBuffer = CreateBuffer(rhi_buffer_desc{
            .size = bufferSize,
            .bufferUsage = rhi_buffer_usage::Uniform,
            .memoryUsage = rhi_memory_usage::CpuToGpu,
        });
        const VkBuffer &vulkanBuffer =
            HandlePool::ResolveData(g_State->m_Buffers, g_State->m_ConstantsUniformBuffer).buffer;

        const array<VkDescriptorBufferInfo, 4> bufferInfos{
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

        auto &descriptorWrites = RegionAlloc::AllocVec<VkWriteDescriptorSet, Array::Size(bufferInfos)>(scratch);
        for (uint32_t i = 0; i < Array::Size(bufferInfos); ++i)
        {
            const VkDescriptorBufferInfo &bufferInfo = bufferInfos[i];
            if (bufferInfo.range)
            {
                InlineVec::Append(descriptorWrites, VkWriteDescriptorSet{
                                                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                        .dstSet = g_State->m_ConstantsDescriptorTable.set,
                                                        .dstBinding = i,
                                                        .descriptorCount = 1,
                                                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                                        .pBufferInfo = &bufferInfo,
                                                    });
            }
        }

        vkUpdateDescriptorSets(g_State->m_Dev, descriptorWrites.size, InlineVec::DataPtr(descriptorWrites), 0, nullptr);
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

auto Rhi::CreateBuffer(const rhi_buffer_desc &desc) -> rhi_buffer
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

    return HandlePool::Acquire(g_State->m_Buffers, bufferData);
}

void Rhi::NameBuffer(rhi_buffer buf, byteview name)
{
    const VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buf);
    VulkanNameHandle(VK_OBJECT_TYPE_BUFFER, (uint64_t)bufferData.buffer, name);
}

void Rhi::DestroyBuffer(rhi_buffer buffer)
{
    VulkanBufferData bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);

    if (bufferData.mapped)
    {
        vkUnmapMemory(g_State->m_Dev, bufferData.memory);
    }
    vkDestroyBuffer(g_State->m_Dev, bufferData.buffer, nullptr);
    vkFreeMemory(g_State->m_Dev, bufferData.memory, nullptr);
}

auto Rhi::GetBufferSize(rhi_buffer buffer) -> uint64_t
{
    return HandlePool::ResolveData(g_State->m_Buffers, buffer).size;
}

auto Rhi::MapBuffer(rhi_buffer buffer) -> char *
{
    const VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);
    if (!bufferData.mapped)
    {
        vkMapMemory(g_State->m_Dev, bufferData.memory, 0, VK_WHOLE_SIZE, 0, (void **)&bufferData.mapped);
    }

    return bufferData.mapped;
}

void Rhi::UnmapBuffer(rhi_buffer buffer)
{
    VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);
    if (bufferData.mapped)
    {
        vkUnmapMemory(g_State->m_Dev, bufferData.memory);
        bufferData.mapped = nullptr;
    }
}

void Rhi::CmdCopyBuffer(rhi_cmdlist cmd, rhi_buffer dst, uint32_t dstOffset, rhi_buffer src, uint32_t srcOffset,
                        uint32_t size)
{
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;

    VulkanBufferData &dstBufferData = HandlePool::ResolveData(g_State->m_Buffers, dst);
    VulkanBufferData &srcBufferData = HandlePool::ResolveData(g_State->m_Buffers, src);

    EnsureHostWritesVisible(cmdbuf, srcBufferData);

    const VkBufferCopy region{
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = size,
    };
    vkCmdCopyBuffer(cmdbuf, srcBufferData.buffer, dstBufferData.buffer, 1, &region);
}

void Rhi::CmdTransitionBuffer(rhi_cmdlist cmd, rhi_buffer buffer, rhi_buffer_state newState)
{
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;
    VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);

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

void Rhi::CmdUavBarrierBuffer(rhi_cmdlist cmd, rhi_buffer buffer)
{
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;
    VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);

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

void Rhi::BufferMarkWritten(rhi_buffer buffer, uint32_t offset, uint32_t size)
{
    VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);

    if (bufferData.dirty)
    {
        bufferData.dirtyBegin = std::min(bufferData.dirtyBegin, offset);
        bufferData.dirtyEnd = Max(bufferData.dirtyBegin, offset + size);
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

auto Rhi::CreateCmdList(rhi_queue_type queueType) -> rhi_cmdlist
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

    rhi_cmdlist cmd = HandlePool::Acquire(g_State->m_CmdLists, cmdData);
    return cmd;
}

void Rhi::ResetCmdList(rhi_cmdlist cmd)
{
    VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
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

void Rhi::NameCmdList(rhi_cmdlist cmd, byteview name)
{
    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    VulkanNameHandle(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cmdData.cmdbuf, name);
}

void Rhi::DestroyCmdList(rhi_cmdlist cmd)
{
    VulkanCmdListData cmdData = HandlePool::ReleaseData(g_State->m_CmdLists, cmd);
    VkCommandPool cmdPool = GetDeviceQueue(cmdData.queueType).cmdPool;
    vkFreeCommandBuffers(g_State->m_Dev, cmdPool, 1, &cmdData.cmdbuf);
}

auto Rhi::CmdSetCheckpoint(rhi_cmdlist cmd, uint64_t data) -> uint64_t
{
    if constexpr (!kRhiCheckpoints)
    {
        return data;
    }

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkCmdSetCheckpointNV);
    fn(cmdData.cmdbuf, reinterpret_cast<void *>(data));

    return data;
}

auto Rhi::GetLastCheckpointData(rhi_queue_type queueType) -> uint64_t
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

auto Rhi::FrameBegin(region_alloc &scratch) -> rhi_cmdlist
{
    SCRATCH_REMEMBER;

    if (g_State->m_RecreateSwapchain)
    {
        vkDeviceWaitIdle(g_State->m_Dev);
        CreateSwapchain(scratch);
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

    const rhi_cmdlist cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
    ResetCmdList(cmd);

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(cmdbuf, &commandBufferBeginInfo));

    return cmd;
}

void Rhi::FrameEnd(region_alloc &scratch)
{
    SCRATCH_REMEMBER;

    rhi_cmdlist cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];

    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;

    VK_CHECK(vkEndCommandBuffer(cmdbuf));

    WriteDescriptorTables(scratch);

    const array<VkPipelineStageFlags, 1> waitStages = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSemaphore acquireSemaphore = g_State->m_SwapchainAcquireSemaphores[g_State->m_FrameIndex];
    const VkSemaphore renderFinishedSemaphore = g_State->m_RenderFinishedSemaphores[g_State->m_SwapchainTextureIndex];

    const array<VkSemaphore, 2> signalSemaphores{
        g_State->m_GraphicsQueue.timeline,
        renderFinishedSemaphore,
    };

    g_State->m_GraphicsQueueCmdDone[g_State->m_FrameIndex] = g_State->m_GraphicsQueue.timelineNext++;

    const VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = Array::Size(signalSemaphores),
        .pSignalSemaphoreValues = &g_State->m_GraphicsQueueCmdDone[g_State->m_FrameIndex],
    };

    if (g_State->m_SwapchainUsable)
    {
        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timelineSubmitInfo,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquireSemaphore,
            .pWaitDstStageMask = waitStages.data,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdbuf,
            .signalSemaphoreCount = Array::Size(signalSemaphores),
            .pSignalSemaphores = signalSemaphores.data,
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

auto Rhi::FrameGetCmdList() -> rhi_cmdlist
{ // TODO: get rid of this
    return g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
}

void Rhi::PassBegin(rhi_pass_desc desc)
{
    rhi_cmdlist cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;

    const VulkanTextureViewData &rtvData = HandlePool::ResolveData(g_State->m_RenderTargetViews, desc.rtv);
    const VulkanTextureData &renderTargetData = HandlePool::ResolveData(g_State->m_Textures, rtvData.texture);

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
        const VulkanTextureViewData &dsvData = HandlePool::ResolveData(g_State->m_DepthStencilViews, desc.dsv);
        const VulkanTextureData &depthStencilData = HandlePool::ResolveData(g_State->m_Textures, dsvData.texture);

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
    rhi_cmdlist cmd = g_State->m_GraphicsQueueCmd[g_State->m_FrameIndex];
    VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    vkCmdEndRendering(cmdbuf);

    cmdData.passConstantHead += CbvOffset(g_State->m_Limits.passConstantSize);
}

auto Rhi::CreateShader(const rhi_shader_desc &desc) -> rhi_shader
{
    // TODO: correct lifetime
    auto alloc = RegionAlloc::Create(16_MiB, 0);

    span<uint32_t> spv = RegionAlloc::AllocArray<uint32_t>(alloc, desc.code);
    MemCpy(spv.data, desc.code.data, desc.code.size);

    const rhi_shader handle = HandlePool::Acquire(g_State->m_Shaders, VulkanShaderData{.spv = spv});
    VulkanShaderData &shaderData = HandlePool::ResolveData(g_State->m_Shaders, handle);

    return handle;
}

void Rhi::DestroyShader(rhi_shader shader)
{
    HandlePool::ReleaseData(g_State->m_Shaders, shader);

#if 0
    VkShaderModule shaderModule = g_State->m_Shaders.ReleaseData(shader);
    vkDestroyShaderModule(g_State->m_Dev, shaderModule, nullptr);
#endif
}

void Rhi::DestroyGraphicsPipeline(rhi_graphics_pipeline pipeline)
{
    auto pipelineData = HandlePool::ReleaseData(g_State->m_GraphicsPipelines, pipeline);
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

void Rhi::CmdBindGraphicsPipeline(rhi_cmdlist cmd, rhi_graphics_pipeline pipeline)
{
    VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VulkanPipelineData &pipelineData = HandlePool::ResolveData(g_State->m_GraphicsPipelines, pipeline);

    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.pipeline);
    cmdData.boundGraphicsPipeline = pipeline;

    array<VkDescriptorSet, 2> descriptorSets{
        g_State->m_TexturesDescriptorTable.set,
        g_State->m_SamplersDescriptorTable.set,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 1,
                            Array::Size(descriptorSets), descriptorSets.data, 0, nullptr);
}

void Rhi::CmdPushGraphicsConstants(rhi_cmdlist cmd, uint32_t offset, rhi_shader_stage stage, byteview data)
{
    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    const VulkanPipelineData &pipelineData =
        HandlePool::ResolveData(g_State->m_GraphicsPipelines, cmdData.boundGraphicsPipeline);

    vkCmdPushConstants(cmdData.cmdbuf, pipelineData.layout, ConvertShaderStageIntoVkShaderStageFlags(stage), offset,
                       data.size, data.data);
}

void Rhi::CmdBindVertexBuffers(rhi_cmdlist cmd, uint32_t firstBinding, span<const rhi_buffer> buffers,
                               span<const uint64_t> offsets)
{
    ASSERT(buffers.size == offsets.size);
    ASSERT(buffers.size <= 4U);

    array<VkBuffer, 4> vkBufs;
    array<VkDeviceSize, 4> vkOffsets;
    for (uint32_t i = 0; i < buffers.size; ++i)
    {
        vkBufs[i] = HandlePool::ResolveData(g_State->m_Buffers, buffers[i]).buffer;
        vkOffsets[i] = offsets[i];
    }

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    vkCmdBindVertexBuffers(cmdData.cmdbuf, firstBinding, buffers.size, vkBufs.data, vkOffsets.data);
}

void Rhi::CmdBindIndexBuffer(rhi_cmdlist cmd, rhi_buffer buffer, uint64_t offset)
{
    const VulkanBufferData &bufferData = HandlePool::ResolveData(g_State->m_Buffers, buffer);

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    vkCmdBindIndexBuffer(cmdData.cmdbuf, bufferData.buffer, offset, VkIndexType::VK_INDEX_TYPE_UINT16);
}

void Rhi::CmdDraw(rhi_cmdlist cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                  uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    CmdDrawInternal(cmdData);
    vkCmdDraw(cmdData.cmdbuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Rhi::CmdDrawIndexed(rhi_cmdlist cmd, uint32_t indexCount, int32_t vertexOffset, uint32_t instanceCount,
                         uint32_t firstIndex, uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);
    CmdDrawInternal(cmdData);
    vkCmdDrawIndexed(cmdData.cmdbuf, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

auto Rhi::CreateSampler(const rhi_sampler_desc &desc) -> rhi_sampler
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

    return HandlePool::Acquire(g_State->m_Samplers, samplerData);
}

void Rhi::DestroySampler(rhi_sampler sampler)
{
    VulkanSamplerData samplerData = HandlePool::ReleaseData(g_State->m_Samplers, sampler);
    vkDestroySampler(g_State->m_Dev, samplerData.sampler, g_State->vkAlloc);
}

auto Rhi::GetBackbufferView() -> rhi_rtv
{
    return g_State->m_SwapchainRTVs[g_State->m_SwapchainTextureIndex];
}

auto Rhi::CreateTexture(const rhi_texture_desc &desc) -> rhi_texture
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

    return HandlePool::Acquire(g_State->m_Textures, textureData);
}

auto Rhi::CreateSampledTextureView(const rhi_texture_view_desc &desc) -> rhi_stv
{
    VulkanTextureData &textureData = HandlePool::ResolveData(g_State->m_Textures, desc.texture);

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

    if (desc.format == rhi_texture_format::None)
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

    const rhi_stv view = HandlePool::Acquire(g_State->m_SampledTextureViews, textureViewData);
    return view;
}

auto Rhi::CreateRenderTargetView(const rhi_render_target_view_desc &desc) -> rhi_rtv
{
    VulkanTextureData &textureData = HandlePool::ResolveData(g_State->m_Textures, desc.texture);

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

    if (desc.format == rhi_texture_format::None)
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
    const rhi_rtv rtv = HandlePool::Acquire(g_State->m_RenderTargetViews, renderTargetViewData);
    return rtv;
}

auto Rhi::CreateDepthStencilView(const rhi_depth_stencil_view_desc &desc) -> rhi_dsv
{
    VulkanTextureData &textureData = HandlePool::ResolveData(g_State->m_Textures, desc.texture);

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

    if (desc.format == rhi_texture_format::None)
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
    const rhi_dsv dsv = HandlePool::Acquire(g_State->m_DepthStencilViews, dsvData);
    return dsv;
}

auto Rhi::GetTexture(rhi_stv srv) -> rhi_texture
{
    return HandlePool::ResolveData(g_State->m_SampledTextureViews, srv).texture;
}

auto Rhi::GetTexture(rhi_rtv rtv) -> rhi_texture
{
    return HandlePool::ResolveData(g_State->m_RenderTargetViews, rtv).texture;
}

auto Rhi::GetTexture(rhi_dsv dsv) -> rhi_texture
{
    return HandlePool::ResolveData(g_State->m_DepthStencilViews, dsv).texture;
}

auto Rhi::GetTextureInfo(rhi_texture texture) -> rhi_texture_info
{
    const VulkanTextureData &textureData = HandlePool::ResolveData(g_State->m_Textures, texture);
    return {
        .width = textureData.extent.width,
        .height = textureData.extent.height,
        .format = ConvertVkFormatIntoTextureFormat(textureData.format),
    };
}

void Rhi::CmdTransitionTexture(rhi_cmdlist cmd, rhi_texture texture, rhi_texture_state newState)
{
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;

    VulkanTextureData &textureData = HandlePool::ResolveData(g_State->m_Textures, texture);

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

void Rhi::DestroyTexture(rhi_texture texture)
{
    VulkanTextureData textureData = HandlePool::ResolveData(g_State->m_Textures, texture);

    ASSERT(textureData.image);
    vkDestroyImage(g_State->m_Dev, textureData.image, g_State->vkAlloc);

    ASSERT(textureData.memory);
    vkFreeMemory(g_State->m_Dev, textureData.memory, g_State->vkAlloc);
}

void Rhi::DestroySampledTextureView(rhi_stv textureView)
{
    const VulkanTextureViewData &textureViewData = HandlePool::ResolveData(g_State->m_SampledTextureViews, textureView);
    ASSERT(textureViewData.imageView);

    vkDestroyImageView(g_State->m_Dev, textureViewData.imageView, g_State->vkAlloc);
}

void Rhi::DestroyRenderTargetView(rhi_rtv textureView)
{
    const VulkanTextureViewData &textureViewData = HandlePool::ResolveData(g_State->m_RenderTargetViews, textureView);
    ASSERT(textureViewData.imageView);

    vkDestroyImageView(g_State->m_Dev, textureViewData.imageView, g_State->vkAlloc);
}

void Rhi::DestroyDepthStencilView(rhi_dsv textureView)
{
    const VulkanTextureViewData &textureViewData = HandlePool::ResolveData(g_State->m_DepthStencilViews, textureView);
    ASSERT(textureViewData.imageView);

    vkDestroyImageView(g_State->m_Dev, textureViewData.imageView, g_State->vkAlloc);
}

void Rhi::CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, rhi_buffer src, uint32_t srcOffset, uint32_t size)
{
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;

    VulkanTextureData &dstTextureData = HandlePool::ResolveData(g_State->m_Textures, dst);
    VulkanBufferData &srcBufferData = HandlePool::ResolveData(g_State->m_Buffers, src);

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

void Rhi::CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, rhi_texture src)
{
    const VkCommandBuffer &cmdbuf = HandlePool::ResolveData(g_State->m_CmdLists, cmd).cmdbuf;

    VulkanTextureData &dstTextureData = HandlePool::ResolveData(g_State->m_Textures, dst);
    VulkanTextureData &srcTextureData = HandlePool::ResolveData(g_State->m_Textures, src);

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

auto Rhi::CreateGraphicsPipeline(region_alloc &scratch, const rhi_graphics_pipeline_desc &desc) -> rhi_graphics_pipeline
{
    SCRATCH_REMEMBER;

    auto &vertexShaderData = HandlePool::ResolveData(g_State->m_Shaders, desc.vs);
    auto &pixelShaderData = HandlePool::ResolveData(g_State->m_Shaders, desc.ps);

    spv_shader vsMan;
    SpvShader::ProcessShader(vsMan, vertexShaderData.spv, rhi_shader_stage::Vertex);

    spv_shader psMan;
    SpvShader::ProcessShader(psMan, pixelShaderData.spv, rhi_shader_stage::Pixel);

    VulkanPipelineData pipelineData = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };

    auto &vertexBindings = RegionAlloc::AllocVec<VkVertexInputBindingDescription, 16>(scratch);
    for (auto &binding : desc.vertexBindings)
    {
        InlineVec::Append(vertexBindings, VkVertexInputBindingDescription{
                                              .binding = binding.binding,
                                              .stride = binding.stride,
                                              .inputRate = ConvertVulkanInputRate(binding.inputRate),
                                          });
    }

    auto &vertexAttributes = RegionAlloc::AllocVec<VkVertexInputAttributeDescription, 16>(scratch);
    for (auto &attribute : desc.vertexAttributes)
    {
        uint32_t location;
        if (!SpvShader::FindLocationBySemantic(vsMan, attribute.semantic, spv_shader_storage_class::Input, &location))
            ASSERT(false);

        InlineVec::Append(vertexAttributes, VkVertexInputAttributeDescription{
                                                .location = location,
                                                .binding = attribute.binding,
                                                .format = ConvertVertexFormatIntoVkFormat(attribute.format),
                                                .offset = attribute.offset,
                                            });
    }

    for (uint32_t id : SpvShader::GetInputIds(psMan))
    {
        byteview semantic;
        if (!SpvShader::FindSemanticById(psMan, id, &semantic))
            ASSERT(false);

        if (Span::StartsWith(semantic, "SV_"_s))
            continue;

        uint32_t location;
        if (!SpvShader::FindLocationBySemantic(vsMan, semantic, spv_shader_storage_class::Output, &location))
            ASSERT(false);

        if (!SpvShader::RewriteLocationForSemantic(psMan, pixelShaderData.spv, semantic,
                                                   spv_shader_storage_class::Input, location))
            ASSERT(false);
    }

    auto createShaderModule = [](span<const uint32_t> spv) -> VkShaderModule {
        const VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = Span::SizeBytes(spv),
            .pCode = spv.data,
        };

        VkShaderModule shaderModule;
        VK_CHECK(vkCreateShaderModule(g_State->m_Dev, &createInfo, nullptr, &shaderModule));

        return shaderModule;
    };

    Erase(vertexShaderData.spv, 0);
    Erase(pixelShaderData.spv, 0);

    const array<VkPipelineShaderStageCreateInfo, 2> stages{
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
        .vertexBindingDescriptionCount = (uint32_t)vertexBindings.size,
        .pVertexBindingDescriptions = InlineVec::DataPtr(vertexBindings),
        .vertexAttributeDescriptionCount = (uint32_t)vertexAttributes.size,
        .pVertexAttributeDescriptions = InlineVec::DataPtr(vertexAttributes),
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

    const array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    const VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = Array::Size(dynamicStates),
        .pDynamicStates = dynamicStates.data,
    };

    auto &colorTargetFormats = RegionAlloc::AllocVec<VkFormat, 16>(scratch);
    for (rhi_texture_format colorTargetFormat : desc.colorTargetFormats)
    {
        InlineVec::Append(colorTargetFormats, ConvertTextureFormatIntoVkFormat(colorTargetFormat, nullptr));
    }

    const VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = (uint32_t)colorTargetFormats.size,
        .pColorAttachmentFormats = InlineVec::DataPtr(colorTargetFormats),
        .depthAttachmentFormat = ConvertTextureFormatIntoVkFormat(desc.depthFormat, nullptr),
    };

#if 0
    ASSERT(desc.bindGroupLayoutsCount <= std::Size(desc.bindGroupLayouts));
    pipelineData.bindGroupLayoutCount = desc.bindGroupLayoutsCount;
    pipelineData.bindGroupLayouts = desc.bindGroupLayouts;
#endif

#if 0
    ASSERT(desc.pushConstantSize <= kRhiMaxPushConstantSize);
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = desc.pushConstantSize,
    };
#endif

    const array<VkDescriptorSetLayout, 3> descriptorSetLayouts = {
        g_State->m_ConstantsDescriptorTable.layout,
        g_State->m_TexturesDescriptorTable.layout,
        g_State->m_SamplersDescriptorTable.layout,
    };

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = Array::Size(descriptorSetLayouts),
        .pSetLayouts = descriptorSetLayouts.data,
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
        .stageCount = Array::Size(stages),
        .pStages = stages.data,
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

    SCRATCH_RESET;
    return HandlePool::Acquire(g_State->m_GraphicsPipelines, pipelineData);
}

void Rhi::NameGraphicsPipeline(rhi_graphics_pipeline pipeline, byteview name)
{
    const VulkanPipelineData &pipelineData = HandlePool::ResolveData(g_State->m_GraphicsPipelines, pipeline);
    VulkanNameHandle(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelineData.pipeline, name);
}

void Rhi::SetFrameConstant(rhi_cmdlist cmd, byteview data)
{
    ASSERT(data.size <= g_State->m_Limits.frameConstantSize);

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.frameConstantHead, data.data, data.size);
}

void Rhi::SetPassConstant(rhi_cmdlist cmd, byteview data)
{
    ASSERT(data.size <= g_State->m_Limits.passConstantSize);

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.passConstantHead, data.data, data.size);
}

void Rhi::SetDrawConstant(rhi_cmdlist cmd, byteview data)
{
    ASSERT(data.size <= g_State->m_Limits.drawConstantSize);

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.drawConstantHead, data.data, data.size);
}

void Rhi::SetLargeDrawConstant(rhi_cmdlist cmd, byteview data)
{
    ASSERT(data.size <= g_State->m_Limits.largeDrawConstantSize);

    const VulkanCmdListData &cmdData = HandlePool::ResolveData(g_State->m_CmdLists, cmd);

    char *mem = MapBuffer(g_State->m_ConstantsUniformBuffer);
    MemCpy(mem + cmdData.largeDrawConstantHead, data.data, data.size);

    if constexpr (false)
    {
        const VulkanBufferData &bufferData =
            HandlePool::ResolveData(g_State->m_Buffers, g_State->m_ConstantsUniformBuffer);
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