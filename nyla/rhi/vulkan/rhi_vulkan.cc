#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/rhi/rhi.h"
#include "vulkan/vulkan_core.h"

// clang-format off
#include "xcb/xcb.h"
#include "vulkan/vulkan_xcb.h"
// clang-format on

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal {

VulkanData vk;
RhiHandles rhi_handles;

VkFormat ConvertVulkanFormat(RhiFormat format) {
  switch (format) {
    case RhiFormat::Float4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RhiFormat::Half2:
      return VK_FORMAT_R16G16_SFLOAT;
    case RhiFormat::SNorm8x4:
      return VK_FORMAT_R8G8B8A8_SNORM;
    case RhiFormat::UNorm8x4:
      return VK_FORMAT_R8G8B8A8_UNORM;
  }
  CHECK(false);
  return static_cast<VkFormat>(0);
}

VkShaderStageFlags ConvertVulkanStageFlags(RhiShaderStage stage_flags) {
  VkShaderStageFlags ret = 0;
  if (Any(stage_flags & RhiShaderStage::Vertex)) {
    ret |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if (Any(stage_flags & RhiShaderStage::Fragment)) {
    ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  return ret;
}

VkBool32 DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                VkDebugUtilsMessageTypeFlagsEXT message_type,
                                const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {
  switch (message_severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
      LOG(ERROR) << callback_data->pMessage;
      DebugBreak;
    }
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
      LOG(WARNING) << callback_data->pMessage;
    }
    default: {
      LOG(INFO) << callback_data->pMessage;
    }
  }
  return VK_FALSE;
}

void VulkanNameHandle(VkObjectType type, uint64_t handle, std::string_view name) {
  auto name_copy = std::string{name};
  const VkDebugUtilsObjectNameInfoEXT name_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = type,
      .objectHandle = reinterpret_cast<uint64_t>(handle),
      .pObjectName = name_copy.c_str(),
  };

  static PFN_vkSetDebugUtilsObjectNameEXT fn = VK_GET_INSTANCE_PROC_ADDR(vkSetDebugUtilsObjectNameEXT);
  fn(vk.dev, &name_info);
}

}  // namespace rhi_vulkan_internal

void RhiInit(const RhiDesc& rhi_desc) {
  constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

  CHECK_LE(rhi_desc.num_frames_in_flight, rhi_max_num_frames_in_flight);
  if (rhi_desc.num_frames_in_flight) {
    vk.num_frames_in_flight = rhi_desc.num_frames_in_flight;
  } else {
    vk.num_frames_in_flight = 2;
  }

  vk.window = rhi_desc.window;

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

  VkDebugUtilsMessengerCreateInfoEXT* debug_messenger_create_info = nullptr;

  if constexpr (rhi_validations) {
    instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkValidationFeaturesEXT* validation_features = nullptr;
    if constexpr (false) {
      instance_extensions.emplace_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

      std::span<VkValidationFeatureEnableEXT> enabled_validations = Tmake(std::array{
          VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
          VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
          VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
          VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
      });

      validation_features = &Tmake(VkValidationFeaturesEXT{
          .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
          .enabledValidationFeatureCount = static_cast<uint32_t>(std::size(enabled_validations)),
          .pEnabledValidationFeatures = enabled_validations.data(),
      });
    }

    debug_messenger_create_info = &Tmake(VkDebugUtilsMessengerCreateInfoEXT{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = validation_features,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugMessengerCallback,
    });

    instance_pNext = debug_messenger_create_info;
  }

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

  VkDebugUtilsMessengerEXT debug_messenger{};
  if constexpr (rhi_validations) {
    VK_CHECK(VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT)(vk.instance, debug_messenger_create_info,
                                                                       nullptr, &debug_messenger));
  }

  uint32_t num_phys_devices = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, nullptr));

  std::vector<VkPhysicalDevice> phys_devs(num_phys_devices);
  VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, phys_devs.data()));

  std::vector<const char*> device_extensions;
  device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  if constexpr (rhi_checkpoints) {
    device_extensions.emplace_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
  }

  for (VkPhysicalDevice phys_dev : phys_devs) {
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(phys_dev, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(phys_dev, nullptr, &extension_count, extensions.data());

    uint32_t missing_extensions = device_extensions.size();
    for (uint32_t j = 0; j < device_extensions.size(); ++j) {
      for (uint32_t i = 0; i < extension_count; ++i) {
        if (strcmp(extensions[i].extensionName, device_extensions[j]) == 0) {
          --missing_extensions;
          break;
        } else {
          // LOG(INFO) << extensions[i].extensionName << " " << device_extensions[j];
        }
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
  VkPhysicalDeviceVulkan11Features v1_1{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &v1_2,
  };
  VkPhysicalDeviceFeatures2 features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &v1_1,
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

  auto init_queue = [](DeviceQueue& queue, RhiQueueType queue_type, std::span<RhiCmdList> cmd) {
    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue.queue_family_index,
    };
    VK_CHECK(vkCreateCommandPool(vk.dev, &command_pool_create_info, nullptr, &queue.cmd_pool));

    queue.timeline = CreateTimeline(0);
    queue.timeline_next = 1;

    for (uint32_t i = 0; i < cmd.size(); ++i) {
      cmd[i] = RhiCreateCmdList(queue_type);
    }
  };
  init_queue(vk.graphics_queue, RhiQueueType::Graphics, std::span{vk.graphics_queue_cmd, vk.num_frames_in_flight});
  init_queue(vk.transfer_queue, RhiQueueType::Transfer, std::span{&vk.transfer_queue_cmd, 1});

  for (size_t i = 0; i < vk.num_frames_in_flight; ++i) {
    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VK_CHECK(vkCreateSemaphore(vk.dev, &semaphore_create_info, nullptr, vk.swapchain_acquire_semaphores + i));
  }

  const VkDescriptorPoolSize descriptor_pool_sizes[]{
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
  };
  const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = 256,
      .poolSizeCount = static_cast<uint32_t>(std::size(descriptor_pool_sizes)),
      .pPoolSizes = descriptor_pool_sizes,
  };
  vkCreateDescriptorPool(vk.dev, &descriptor_pool_create_info, nullptr, &vk.descriptor_pool);

  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = xcb_connect(nullptr, nullptr),
      .window = vk.window.handle,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr, &vk.surface));

  CreateSwapchain();
}

uint32_t RhiGetMinUniformBufferOffsetAlignment() {
  return vk.phys_dev_props.limits.minUniformBufferOffsetAlignment;
}

uint32_t RhiGetSurfaceWidth() {
  return vk.surface_extent.width;
}

uint32_t RhiGetSurfaceHeight() {
  return vk.surface_extent.height;
}

}  // namespace nyla

#undef VK_CHECK