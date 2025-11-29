#include "nyla/rhi/rhi_vulkan.h"

#include <cstdint>
#include <cstring>

#include "absl/log/check.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/handle_pool.h"
#include "nyla/rhi/rhi.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

// clang-format off
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_window_t;
typedef uint32_t xcb_visualid_t;
#include "vulkan/vulkan_xcb.h"
// clang-format on

#include <vector>

constexpr inline uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

#define VK_CHECK(res) CHECK_EQ(res, VK_SUCCESS) << "Vulkan error: " << string_VkResult(res);

namespace nyla {

namespace {

struct HandleSlot {
  void* data;
  uint32_t gen;
};

struct DeviceQueue {
  VkQueue queue;
  uint32_t queue_family_index;

  VkSemaphore timeline;
  uint64_t timeline_next;

  VkCommandPool cmd_pool;
  VkCommandBuffer cmd[RhiDesc::kMaxFramesInFlight];
};

static struct {
  VkInstance instance;
  VkDevice dev;
  VkPhysicalDevice phys_dev;
  VkPhysicalDeviceProperties phys_dev_props;
  VkPhysicalDeviceMemoryProperties phys_dev_mem_props;

  PlatformWindow window;
  VkSurfaceKHR surface;
  VkSurfaceFormatKHR surface_format;
  VkSwapchainKHR swapchain;
  VkImage swapchain_images[8];
  VkImageView swapchain_image_views[8];
  uint32_t swapchain_image_count;

  DeviceQueue graphics_queue;
  DeviceQueue transfer_queue;

  uint32_t num_frames_in_flight;
} vk;

static HandlePool<VkShaderModule, 16> shader_hpool;

struct VulkanPipeline {
  VkPipelineLayout layout;
  VkPipeline pipeline;
};
static HandlePool<VulkanPipeline, 16> gfx_pipeline_hpool;

#if !defined(NDEBUG)

static VkBool32 DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                       VkDebugUtilsMessageTypeFlagsEXT message_type,
                                       const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data);

#endif

static VkSemaphore CreateTimeline(uint64_t initial_value) {
  const VkSemaphoreTypeCreateInfo timeline_create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initial_value,
  };

  const VkSemaphoreCreateInfo semaphore_create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timeline_create_info,
  };

  VkSemaphore semaphore;
  vkCreateSemaphore(vk.dev, &semaphore_create_info, nullptr, &semaphore);
  return semaphore;
}

static void CreateSwapchain() {
  VkSwapchainKHR old_swapchain = vk.swapchain;
  VkImageView old_image_views[std::size(vk.swapchain_image_views)];
  uint32_t old_images_views_count = vk.swapchain_image_count;
  memcpy(old_image_views, vk.swapchain_image_views, std::size(vk.swapchain_image_views) * old_images_views_count);

  VkSurfaceCapabilitiesKHR surface_capabilities;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys_dev, vk.surface, &surface_capabilities));

  vk.surface_format = [] {
    uint32_t surface_format_count;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_dev, vk.surface, &surface_format_count, nullptr));
    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_dev, vk.surface, &surface_format_count, surface_formats.data());

    auto it = std::find_if(surface_formats.begin(), surface_formats.end(), [](VkSurfaceFormatKHR surface_format) {
      return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
             surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    CHECK(it != surface_formats.end());
    return *it;
  }();

  VkPresentModeKHR present_mode = [] {
    std::vector<VkPresentModeKHR> present_modes;
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_dev, vk.surface, &present_mode_count, nullptr);
    CHECK(present_mode_count);

    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_dev, vk.surface, &present_mode_count, present_modes.data());

    return VK_PRESENT_MODE_FIFO_KHR;  // TODO:
  }();

  VkExtent2D surface_extent = [surface_capabilities] {
    if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
      return surface_capabilities.currentExtent;
    }

    const PlatformWindowSize window_size = PlatformGetWindowSize(vk.window);
    return VkExtent2D{
        .width = std::clamp(window_size.width, surface_capabilities.minImageExtent.width,
                            surface_capabilities.maxImageExtent.width),
        .height = std::clamp(window_size.height, surface_capabilities.minImageExtent.height,
                             surface_capabilities.maxImageExtent.height),
    };
  }();

  uint32_t swapchain_min_image_count = surface_capabilities.minImageCount + 1;
  if (surface_capabilities.maxImageCount) {
    swapchain_min_image_count = std::min(surface_capabilities.maxImageCount, swapchain_min_image_count);
  }

  const VkSwapchainCreateInfoKHR create_info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = vk.surface,
      .minImageCount = swapchain_min_image_count,
      .imageFormat = vk.surface_format.format,
      .imageColorSpace = vk.surface_format.colorSpace,
      .imageExtent = surface_extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = surface_capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = present_mode,
      .clipped = VK_TRUE,
      .oldSwapchain = vk.swapchain,
  };
  VK_CHECK(vkCreateSwapchainKHR(vk.dev, &create_info, nullptr, &vk.swapchain));

  uint32_t swapchain_image_count;
  vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &swapchain_image_count, nullptr);

  CHECK_LE(swapchain_image_count, std::size(vk.swapchain_images));
  vkGetSwapchainImagesKHR(vk.dev, vk.swapchain, &swapchain_image_count, vk.swapchain_images);

  for (size_t i = 0; i < swapchain_image_count; ++i) {
    const VkImageViewCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk.swapchain_images[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk.surface_format.format,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VK_CHECK(vkCreateImageView(vk.dev, &create_info, nullptr, &vk.swapchain_image_views[i]));
  }

  if (old_swapchain) {
    vkDeviceWaitIdle(vk.dev);

    for (const VkImageView image_view : old_image_views) {
      vkDestroyImageView(vk.dev, image_view, nullptr);
    }
    vkDestroySwapchainKHR(vk.dev, old_swapchain, nullptr);
  }
}

}  // namespace

void RhiInit(RhiDesc rhi_desc) {
  CHECK_LT(rhi_desc.num_frames_in_flight, RhiDesc::kMaxFramesInFlight);
  if (!rhi_desc.num_frames_in_flight) {
    rhi_desc.num_frames_in_flight = RhiDesc::kMaxFramesInFlight;
  }
  vk.num_frames_in_flight = rhi_desc.num_frames_in_flight;

  const VkApplicationInfo app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "nyla",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "nyla",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_4,
  };

  const void* instance_pNext = nullptr;
  std::vector<const char*> instance_extensions;
  std::vector<const char*> instance_layers;

  instance_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
  instance_extensions.emplace_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);

#if !defined(NDEBUG)
  instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  instance_extensions.emplace_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

  const VkValidationFeatureEnableEXT enabled_validations[] = {
      VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
      VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
  };

  const VkValidationFeaturesEXT validation_features{
      .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
      .enabledValidationFeatureCount = static_cast<uint32_t>(std::size(enabled_validations)),
      .pEnabledValidationFeatures = enabled_validations,
  };

  const VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext = &validation_features,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = DebugMessengerCallback,
  };

  instance_pNext = &debug_messenger_create_info;
#endif

  const VkInstanceCreateInfo instance_create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = instance_pNext,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
      .ppEnabledLayerNames = instance_layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
      .ppEnabledExtensionNames = instance_extensions.data(),
  };
  VK_CHECK(vkCreateInstance(&instance_create_info, nullptr, &vk.instance));

#if !defined(NDEBUG)
  VkDebugUtilsMessengerEXT debug_messenger;
  CHECK_EQ(VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT)(vk.instance, &debug_messenger_create_info, nullptr,
                                                                     &debug_messenger),
           VK_SUCCESS);
#endif

  uint32_t num_phys_devices = 0;
  CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, nullptr), VK_SUCCESS);

  std::vector<VkPhysicalDevice> phys_devs(num_phys_devices);
  CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, phys_devs.data()), VK_SUCCESS);

  std::vector<const char*> device_extensions;
  device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  for (VkPhysicalDevice phys_dev : phys_devs) {
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(phys_dev, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(phys_dev, nullptr, &extension_count, extensions.data());

    uint32_t missing_extensions = device_extensions.size();
    for (uint32_t i = 0; i < extension_count; ++i) {
      for (uint32_t j = 0; j < device_extensions.size(); ++j) {
        if (strcmp(extensions[i].extensionName, device_extensions[j]) == 0) {
          --missing_extensions;
        }
        break;
      }
    }
    if (missing_extensions) {
      continue;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys_dev, &props);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys_dev, &mem_props);

    uint32_t queue_family_prop_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &queue_family_prop_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_prop_count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &queue_family_prop_count, queue_family_properties.data());

    constexpr static uint32_t kInvalidQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
    uint32_t graphics_queue_index = kInvalidQueueFamilyIndex;
    uint32_t transfer_queue_index = kInvalidQueueFamilyIndex;

    for (size_t i = 0; i < queue_family_prop_count; ++i) {
      VkQueueFamilyProperties& props = queue_family_properties[i];
      if (!props.queueCount) {
        continue;
      }

      if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        if (graphics_queue_index == kInvalidQueueFamilyIndex) {
          graphics_queue_index = i;
        }
        continue;
      }

      if (props.queueFlags & VK_QUEUE_COMPUTE_BIT) {
        continue;
      }

      if (props.queueFlags & VK_QUEUE_TRANSFER_BIT) {
        if (transfer_queue_index == kInvalidQueueFamilyIndex) {
          transfer_queue_index = i;
        }
        continue;
      }
    }

    if (graphics_queue_index == kInvalidQueueFamilyIndex) {
      continue;
    }

    // TODO: pick best device
    vk.phys_dev = phys_dev;
    vk.phys_dev_props = props;
    vk.phys_dev_mem_props = mem_props;
    vk.graphics_queue.queue_family_index = graphics_queue_index;
    vk.transfer_queue.queue_family_index = transfer_queue_index;

    break;
  }

  CHECK(vk.phys_dev);

  const float queue_priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  if (vk.transfer_queue.queue_family_index == kInvalidIndex) {
    queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.graphics_queue.queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    });
  } else {
    queue_create_infos.reserve(2);

    queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.graphics_queue.queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    });

    queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.transfer_queue.queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    });
  }

  VkPhysicalDeviceVulkan14Features v1_4{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
  };
  VkPhysicalDeviceVulkan13Features v1_3{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &v1_4,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE,
  };
  VkPhysicalDeviceVulkan12Features v1_2{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &v1_3,
      .scalarBlockLayout = VK_TRUE,
      .timelineSemaphore = VK_TRUE,
  };
  VkPhysicalDeviceFeatures2 features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &v1_2,
  };

  const VkDeviceCreateInfo device_create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features,
      .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(),
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
  };
  VK_CHECK(vkCreateDevice(vk.phys_dev, &device_create_info, nullptr, &vk.dev));

  vkGetDeviceQueue(vk.dev, vk.graphics_queue.queue_family_index, 0, &vk.graphics_queue.queue);

  if (vk.transfer_queue.queue_family_index == kInvalidIndex) {
    vk.transfer_queue.queue_family_index = vk.graphics_queue.queue_family_index;
    vk.transfer_queue.queue = vk.graphics_queue.queue;
  } else {
    vkGetDeviceQueue(vk.dev, vk.transfer_queue.queue_family_index, 0, &vk.transfer_queue.queue);
  }

  auto init_queue = [](DeviceQueue& queue, uint32_t num_command_buffers) {
    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue.queue_family_index,
    };
    VK_CHECK(vkCreateCommandPool(vk.dev, &command_pool_create_info, nullptr, &queue.cmd_pool));

    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = queue.cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = num_command_buffers,
    };
    VK_CHECK(vkAllocateCommandBuffers(vk.dev, &alloc_info, queue.cmd));

    queue.timeline = CreateTimeline(0);
    queue.timeline_next = 1;
  };
  init_queue(vk.graphics_queue, 1);
  init_queue(vk.transfer_queue, vk.num_frames_in_flight);
}

RhiShader RhiCreateShader(RhiShaderDesc desc) {
  const VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = desc.code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(desc.code.data()),
  };

  VkShaderModule shader_module;
  VK_CHECK(vkCreateShaderModule(vk.dev, &create_info, nullptr, &shader_module));

  return static_cast<RhiShader>(HandleAcquire(shader_hpool, shader_module));
}

void RhiDestroyShader(RhiShader shader) {
  VkShaderModule shader_module = HandleGetData(shader_hpool, shader);
  vkDestroyShaderModule(vk.dev, shader_module, nullptr);

  HandleRelease(shader_hpool, shader);
}

void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline pipeline) {
  auto& data = HandleGetData(gfx_pipeline_hpool, pipeline);

  if (data.layout) {
    vkDeviceWaitIdle(vk.dev);
    vkDestroyPipelineLayout(vk.dev, data.layout, nullptr);
  }
  if (data.pipeline) {
    vkDeviceWaitIdle(vk.dev);
    vkDestroyPipeline(vk.dev, data.pipeline, nullptr);
  }

  HandleRelease(gfx_pipeline_hpool, pipeline);
}

RhiGraphicsPipeline RhiCreateGraphicsPipeline(RhiGraphicsPipelineDesc desc) {
  VkVertexInputBindingDescription vertex_bindings[std::size(desc.vertex_bindings)];
  CHECK_LE(desc.vertex_bindings_count, std::size(desc.vertex_bindings));
  for (uint32_t i = 0; i < desc.vertex_bindings_count; ++i) {
    const auto& binding = desc.vertex_bindings[i];
    vertex_bindings[i] = VkVertexInputBindingDescription{
        .binding = binding.binding,
        .stride = binding.stride,
        .inputRate = ConvertVulkanInputRate(binding.input_rate),
    };
  }

  VkVertexInputAttributeDescription vertex_attributes[std::size(desc.vertex_attributes)];
  CHECK_LE(desc.vertex_attribute_count, std::size(desc.vertex_attributes));
  for (uint32_t i = 0; i < desc.vertex_attribute_count; ++i) {
    const auto& attribute = desc.vertex_attributes[i];
    vertex_attributes[i] = VkVertexInputAttributeDescription{
        .location = attribute.location,
        .binding = attribute.binding,
        .format = ConvertVulkanFormat(attribute.format),
        .offset = attribute.offset,
    };
  }

  const VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = desc.bind_layout_count,
      .pVertexBindingDescriptions = vertex_bindings,
      .vertexAttributeDescriptionCount = desc.vertex_attribute_count,
      .pVertexAttributeDescriptions = vertex_attributes,
  };

  const VkPipelineRasterizationStateCreateInfo rasterizer_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = ConvertVulkanCullMode(desc.cull_mode),
      .frontFace = ConvertVulkanFrontFace(desc.front_face),
      .lineWidth = 1.0f,
  };

  const VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  const VkPipelineViewportStateCreateInfo viewport_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };

  const VkPipelineMultisampleStateCreateInfo multisampling_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
      .minSampleShading = 1.0f,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable = VK_FALSE,
  };

  const VkPipelineColorBlendAttachmentState color_blend_attachment{
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

  const VkPipelineColorBlendStateCreateInfo color_blending_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
      .blendConstants = {},
  };

  const auto dynamic_states = std::to_array<VkDynamicState>({
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  });

  const VkPipelineDynamicStateCreateInfo dynamic_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = dynamic_states.size(),
      .pDynamicStates = dynamic_states.data(),
  };

  const VkPipelineRenderingCreateInfo pipeline_rendering_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &vk.surface_format.format,
  };

  VkPipelineShaderStageCreateInfo stages[2];
  uint32_t stage_count = 0;

  if (HandleIsSet(desc.vert_shader)) {
    stages[stage_count++] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = HandleGetData(shader_hpool, desc.vert_shader),
        .pName = "main",
    };
  }
  if (HandleIsSet(desc.frag_shader)) {
    stages[stage_count++] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = HandleGetData(shader_hpool, desc.frag_shader),
        .pName = "main",
    };
  }

  VkDescriptorSetLayout descriptor_set_layouts[std::size(desc.bind_layouts)];
  CHECK_LE(desc.bind_layout_count, std::size(desc.bind_layouts));

  for (uint32_t ilayout = 0; ilayout < desc.bind_layout_count; ++ilayout) {
    const auto& bind_layout = desc.bind_layouts[ilayout];
    CHECK_LE(bind_layout.binding_count, std::size(bind_layout.bindings));
    VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[std::size(bind_layout.bindings)];

    for (uint32_t ibinding = 0; ibinding < bind_layout.binding_count; ++ibinding) {
      const auto& binding = bind_layout.bindings[ibinding];
      descriptor_set_layout_bindings[ibinding] = {
          .binding = binding.binding,
          .descriptorType = ConvertVulkanBindingType(binding.type),
          .descriptorCount = binding.array_size,
          .stageFlags = ConvertVulkanStageFlags(binding.stage_flags),
      };
    }

    const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = bind_layout.binding_count,
        .pBindings = descriptor_set_layout_bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(vk.dev, &descriptor_set_layout_create_info, nullptr,
                                         descriptor_set_layouts + ilayout));
  }

  const VkPushConstantRange push_constant_range{
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = kRhiPushConstantMaxSize,
  };

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = desc.bind_layout_count,
      .pSetLayouts = descriptor_set_layouts,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant_range,
  };

  VkPipelineLayout layout;
  vkCreatePipelineLayout(vk.dev, &pipeline_layout_create_info, nullptr, &layout);

  const VkGraphicsPipelineCreateInfo pipeline_create_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pipeline_rendering_create_info,
      .stageCount = stage_count,
      .pStages = stages,
      .pVertexInputState = &vertex_input_state_create_info,
      .pInputAssemblyState = &input_assembly_create_info,
      .pViewportState = &viewport_state_create_info,
      .pRasterizationState = &rasterizer_create_info,
      .pMultisampleState = &multisampling_create_info,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_blending_create_info,
      .pDynamicState = &dynamic_state_create_info,
      .layout = layout,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = -1,
  };

  VkPipeline pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

  return static_cast<RhiGraphicsPipeline>(HandleAcquire(gfx_pipeline_hpool, VulkanPipeline{layout, pipeline}));
}

VkShaderModule Vulkan_CreateShaderModule(const std::vector<char>& code) {
  const VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };

  VkShaderModule shader_module;
  VK_CHECK(vkCreateShaderModule(vk.dev, &create_info, nullptr, &shader_module));
  return shader_module;
}

}  // namespace nyla

#undef VK_CHECK