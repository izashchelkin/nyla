#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "vulkan/vulkan_core.h"

// clang-format off
#include "xcb/xcb.h"
#include "vulkan/vulkan_xcb.h"
// clang-format on

namespace nyla
{

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal
{

VulkanData vk;
RhiHandles rhiHandles;

auto ConvertRhiVertexFormatIntoVkFormat(RhiVertexFormat format) -> VkFormat
{
    switch (format)
    {
    case RhiVertexFormat::None:
        break;

    case RhiVertexFormat::R32G32B32A32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    CHECK(false);
    return static_cast<VkFormat>(0);
}

auto ConvertRhiShaderStageIntoVkShaderStageFlags(RhiShaderStage stageFlags) -> VkShaderStageFlags
{
    VkShaderStageFlags ret = 0;
    if (Any(stageFlags & RhiShaderStage::Vertex))
    {
        ret |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (Any(stageFlags & RhiShaderStage::Fragment))
    {
        ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return ret;
}

auto DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                            const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData) -> VkBool32
{
    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
        LOG(ERROR) << callbackData->pMessage;
        DebugBreak;
    }
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
        LOG(WARNING) << callbackData->pMessage;
    }
    default: {
        LOG(INFO) << callbackData->pMessage;
    }
    }
    return VK_FALSE;
}

void VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name)
{
    auto nameCopy = std::string{name};
    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = reinterpret_cast<uint64_t>(handle),
        .pObjectName = nameCopy.c_str(),
    };

    static auto fn = VK_GET_INSTANCE_PROC_ADDR(vkSetDebugUtilsObjectNameEXT);
    fn(vk.dev, &nameInfo);
}

} // namespace rhi_vulkan_internal

void RhiInit(const RhiDesc &rhiDesc)
{
    constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    CHECK_LE(rhiDesc.numFramesInFlight, kRhiMaxNumFramesInFlight);
    if (rhiDesc.numFramesInFlight)
    {
        vk.numFramesInFlight = rhiDesc.numFramesInFlight;
    }
    else
    {
        vk.numFramesInFlight = 2;
    }

    vk.flags = rhiDesc.flags;
    vk.window = rhiDesc.window;

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
    enabledInstanceExtensions.emplace_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);

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

        LOG(INFO);
        LOG(INFO) << instanceExtensionsCount << " Instance Extensions available";
        for (uint32_t i = 0; i < instanceExtensionsCount; ++i)
        {
            const auto &extension = instanceExtensions[i];
            LOG(INFO) << "" << extension.extensionName;
        }
    }

    LOG(INFO);
    LOG(INFO) << layers.size() << " Layers available";
    for (uint32_t i = 0; i < layerCount; ++i)
    {
        const auto &layer = layers[i];
        LOG(INFO) << "    " << layer.layerName;

        std::vector<VkExtensionProperties> layerExtensions;
        uint32_t layerExtensionProperties;
        vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, nullptr);

        layerExtensions.resize(layerExtensionProperties);
        vkEnumerateInstanceExtensionProperties(layer.layerName, &layerExtensionProperties, layerExtensions.data());

        for (uint32_t i = 0; i < layerExtensionProperties; ++i)
        {
            const auto &extension = layerExtensions[i];
            LOG(INFO) << "        " << extension.extensionName;
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
    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &vk.instance));

    if constexpr (kRhiValidations)
    {
        auto createDebugUtilsMessenger = VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT);
        VK_CHECK(createDebugUtilsMessenger(vk.instance, &debugMessengerCreateInfo, nullptr, &debugMessenger));
    }

    uint32_t numPhysDevices = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &numPhysDevices, nullptr));

    std::vector<VkPhysicalDevice> physDevs(numPhysDevices);
    VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &numPhysDevices, physDevs.data()));

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
                    LOG(INFO) << "Found device extension: " << deviceExtension;
                    --missingExtensions;
                    break;
                }
            }
        }

        if (missingExtensions)
        {
            LOG(WARNING) << "Missing " << missingExtensions << " extensions";
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
        vk.physDev = physDev;
        vk.physDevProps = props;
        vk.physDevMemProps = memProps;
        vk.graphicsQueue.queueFamilyIndex = graphicsQueueIndex;
        vk.transferQueue.queueFamilyIndex = transferQueueIndex;

        break;
    }

    CHECK(vk.physDev);

    const float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    if (vk.transferQueue.queueFamilyIndex == kInvalidIndex)
    {
        queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.graphicsQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }
    else
    {
        queueCreateInfos.reserve(2);

        queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.graphicsQueue.queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });

        queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.transferQueue.queueFamilyIndex,
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
        .shaderDemoteToHelperInvocation = VK_TRUE,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features v12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &v13,
        .timelineSemaphore = VK_TRUE,
    };
    VkPhysicalDeviceVulkan11Features v11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &v12,
    };

    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR fifoLatestReadyFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext = &v11,
        .presentModeFifoLatestReady = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 features{
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
    VK_CHECK(vkCreateDevice(vk.physDev, &deviceCreateInfo, nullptr, &vk.dev));

    vkGetDeviceQueue(vk.dev, vk.graphicsQueue.queueFamilyIndex, 0, &vk.graphicsQueue.queue);

    if (vk.transferQueue.queueFamilyIndex == kInvalidIndex)
    {
        vk.transferQueue.queueFamilyIndex = vk.graphicsQueue.queueFamilyIndex;
        vk.transferQueue.queue = vk.graphicsQueue.queue;
    }
    else
    {
        vkGetDeviceQueue(vk.dev, vk.transferQueue.queueFamilyIndex, 0, &vk.transferQueue.queue);
    }

    auto initQueue = [](DeviceQueue &queue, RhiQueueType queueType, std::span<RhiCmdList> cmd) -> void {
        const VkCommandPoolCreateInfo commandPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue.queueFamilyIndex,
        };
        VK_CHECK(vkCreateCommandPool(vk.dev, &commandPoolCreateInfo, nullptr, &queue.cmdPool));

        queue.timeline = CreateTimeline(0);
        queue.timelineNext = 1;

        for (auto &i : cmd)
        {
            i = RhiCreateCmdList(queueType);
        }
    };
    initQueue(vk.graphicsQueue, RhiQueueType::Graphics, std::span{vk.graphicsQueueCmd.data(), vk.numFramesInFlight});
    initQueue(vk.transferQueue, RhiQueueType::Transfer, std::span{&vk.transferQueueCmd, 1});

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (size_t i = 0; i < vk.numFramesInFlight; ++i)
    {
        VK_CHECK(vkCreateSemaphore(vk.dev, &semaphoreCreateInfo, nullptr, vk.swapchainAcquireSemaphores.data() + i));
    }
    for (size_t i = 0; i < kRhiMaxNumSwapchainTextures; ++i)
    {
        VK_CHECK(vkCreateSemaphore(vk.dev, &semaphoreCreateInfo, nullptr, vk.renderFinishedSemaphores.data() + i));
    }

    const std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
    };
    const VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 256,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
        .pPoolSizes = descriptorPoolSizes.data(),
    };
    vkCreateDescriptorPool(vk.dev, &descriptorPoolCreateInfo, nullptr, &vk.descriptorPool);

    const VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = xcb_connect(nullptr, nullptr),
        .window = vk.window.handle,
    };
    VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surfaceCreateInfo, nullptr, &vk.surface));

    CreateSwapchain();
}

auto RhiGetMinUniformBufferOffsetAlignment() -> uint32_t
{
    return vk.physDevProps.limits.minUniformBufferOffsetAlignment;
}

auto RhiGetSurfaceWidth() -> uint32_t
{
    return vk.surfaceExtent.width;
}

auto RhiGetSurfaceHeight() -> uint32_t
{
    return vk.surfaceExtent.height;
}

} // namespace nyla

#undef VK_CHECK