#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "nyla/commons/assert.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/log.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_pipeline.h"
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
        .window = g_Platform.GetImpl()->WinGetHandle(),
    };
    VK_CHECK(vkCreateXcbSurfaceKHR(m_Instance, &surfaceCreateInfo, m_Alloc, &m_Surface));
#else
    const VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = g_Platform->GetImpl()->GetHInstance(),
        .hwnd = g_Platform->GetImpl()->WinGetHandle(),
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

    auto initDescriptorTable = [this](DescriptorTable &table,
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

auto Rhi::Impl::GetMinUniformBufferOffsetAlignment() -> uint32_t
{
    return m_PhysDevProps.limits.minUniformBufferOffsetAlignment;
}

auto Rhi::Impl::GetOptimalBufferCopyOffsetAlignment() -> uint32_t
{
    return m_PhysDevProps.limits.optimalBufferCopyOffsetAlignment;
}

//

void Rhi::Init(const RhiInitDesc &rhiDesc)
{
    NYLA_ASSERT(!m_Impl);
    m_Impl = new Impl{};
    m_Impl->Init(rhiDesc);
}

auto Rhi::GetMinUniformBufferOffsetAlignment() -> uint32_t
{
    return m_Impl->GetMinUniformBufferOffsetAlignment();
}

auto Rhi::GetOptimalBufferCopyOffsetAlignment() -> uint32_t
{
    return m_Impl->GetOptimalBufferCopyOffsetAlignment();
}

Rhi g_Rhi{};

} // namespace nyla

#undef VK_CHECK