#include <cstdint>
#include <cstring>

#include "absl/log/check.h"
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
  DeviceQueue graphics_queue;
  DeviceQueue transfer_queue;

  uint32_t num_frames_in_flight;
} vk;

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

void Init(RhiDesc rhi_desc) {
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

}  // namespace

Rhi rhi = {
    .Init = Init,
};

}  // namespace nyla

#undef VK_CHECK
