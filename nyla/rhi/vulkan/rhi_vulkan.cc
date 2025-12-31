#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "nyla/commons/assert.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/log.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "vulkan/vulkan_core.h"

#if defined(__linux__)
// clang-format off
#include "xcb/xcb.h"
#include "vulkan/vulkan_xcb.h"
// clang-format on
#else
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "vulkan/vulkan_win32.h"

#include "nyla/platform/windows/platform_windows.h"
// clang-format on
#endif

namespace nyla
{

using namespace rhi_vulkan_internal;

auto Rhi::Impl::ConvertVertexFormatIntoVkFormat(RhiVertexFormat format) -> VkFormat
{
    switch (format)
    {
    case RhiVertexFormat::None:
        break;
    case RhiVertexFormat::R32G32B32A32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RhiVertexFormat::R32G32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    }
    NYLA_ASSERT(false);
    return static_cast<VkFormat>(0);
}

auto Rhi::Impl::ConvertShaderStageIntoVkShaderStageFlags(RhiShaderStage stageFlags) -> VkShaderStageFlags
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

auto Rhi::Impl::DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                       VkDebugUtilsMessageTypeFlagsEXT messageType,
                                       const VkDebugUtilsMessengerCallbackDataEXT *callbackData) -> VkBool32
{
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
        NYLA_LOG("%s", callbackData->pMessage);
        DebugBreak();
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

void Rhi::Impl::VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name)
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

void Rhi::Impl::Init(const RhiInitDesc &rhiDesc)
{
    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    NYLA_ASSERT(rhiDesc.numFramesInFlight <= kRhiMaxNumFramesInFlight);
    if (rhiDesc.numFramesInFlight)
    {
        m_NumFramesInFlight = rhiDesc.numFramesInFlight;
    }
    else
    {
        m_NumFramesInFlight = 2;
    }

    m_Flags = rhiDesc.flags;
    m_Window = rhiDesc.window;

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
        .pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                              VkDebugUtilsMessageTypeFlagsEXT messageType,
                              const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData) -> VkBool32 {
            return ((Rhi::Impl *)userData)->DebugMessengerCallback(messageSeverity, messageType, callbackData);
        },
        .pUserData = this,
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

    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR fifoLatestReadyFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext = &v11,
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

    auto initQueue = [this](DeviceQueue &queue, RhiQueueType queueType, std::span<RhiCmdList> cmd) -> void {
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
    initQueue(m_GraphicsQueue, RhiQueueType::Graphics, std::span{m_GraphicsQueueCmd.data(), m_NumFramesInFlight});
    initQueue(m_TransferQueue, RhiQueueType::Transfer, std::span{&m_TransferQueueCmd, 1});

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (size_t i = 0; i < m_NumFramesInFlight; ++i)
    {
        VK_CHECK(vkCreateSemaphore(m_Dev, &semaphoreCreateInfo, nullptr, m_SwapchainAcquireSemaphores.data() + i));
    }
    for (size_t i = 0; i < kRhiMaxNumSwapchainTextures; ++i)
    {
        VK_CHECK(vkCreateSemaphore(m_Dev, &semaphoreCreateInfo, nullptr, m_RenderFinishedSemaphores.data() + i));
    }

    const std::array<VkDescriptorPoolSize, 4> descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 256},
    };

    const VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags =
            VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT /* | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT */,
        .maxSets = 256,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
        .pPoolSizes = descriptorPoolSizes.data(),
    };
    vkCreateDescriptorPool(m_Dev, &descriptorPoolCreateInfo, nullptr, &m_DescriptorPool);

#if defined(__linux__)
    const VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = xcb_connect(nullptr, nullptr),
        .window = static_cast<xcb_window_t>(m_Window.handle),
    };
    VK_CHECK(vkCreateXcbSurfaceKHR(m_Instance, &surfaceCreateInfo, m_Alloc, &m_Surface));
#else
    const VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = g_Platform->GetImpl()->GetHInstance(),
        .hwnd = reinterpret_cast<HWND>(m_Window.handle),
    };
    vkCreateWin32SurfaceKHR(m_Instance, &surfaceCreateInfo, m_Alloc, &m_Surface);
#endif

    CreateSwapchain();
}

auto Rhi::Impl::GetMinUniformBufferOffsetAlignment() -> uint32_t
{
    return m_PhysDevProps.limits.minUniformBufferOffsetAlignment;
}

auto Rhi::Impl::GetOptimalBufferCopyOffsetAlignment() -> uint32_t
{
    return m_PhysDevProps.limits.optimalBufferCopyOffsetAlignment;
}

} // namespace nyla

#undef VK_CHECK