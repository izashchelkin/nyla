#include "nyla/vulkan/vulkan.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <array>
#include <cstdint>
#include <ctime>
#include <limits>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/commons/os/clock.h"
#include "nyla/vulkan/renderdoc.h"

// clang-format off
#include "xcb/xcb.h"
#define VK_USE_PLATFORM_XCB_KHR
#include "vulkan/vulkan_xcb.h"
// clang-format on

namespace nyla {

VkState vk;
static uint32_t fps = 0;

uint32_t GetFps() {
  return fps;
}

static void CreateSwapchain();

#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)

static VkBool32 DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                       VkDebugUtilsMessageTypeFlagsEXT message_type,
                                       const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data);

#endif

void Vulkan_Initialize(const char* appname) {
  if (!vk.frames_inflight) {
    vk.frames_inflight = 2;
  }

#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)
#endif

  {
    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = appname,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "nyla",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    const auto instance_extensions = std::to_array({
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,

#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
#endif
    });

#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)
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
#endif

    std::vector<const char*> layers;

#if 0
    layers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif

    const VkInstanceCreateInfo instance_create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef NYLA_VULKAN_DEBUG
        .pNext = &debug_messenger_create_info,
#endif
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = instance_extensions.size(),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    VK_CHECK(vkCreateInstance(&instance_create_info, nullptr, &vk.instance));

#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)
    VkDebugUtilsMessengerEXT debug_messenger;
    CHECK_EQ(VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT)(vk.instance, &debug_messenger_create_info,
                                                                       nullptr, &debug_messenger),
             VK_SUCCESS);
#endif
  };

  vk.phys_device = [=]() {
    uint32_t num_phys_devices = 0;
    CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, nullptr), VK_SUCCESS);

    std::vector<VkPhysicalDevice> phys_devices(num_phys_devices);
    CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, phys_devices.data()), VK_SUCCESS);

    return phys_devices.front();
  }();

  vkGetPhysicalDeviceProperties(vk.phys_device, &vk.phys_device_props);

  uint32_t queue_family_property_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vk.phys_device, &queue_family_property_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_property_count);
  vkGetPhysicalDeviceQueueFamilyProperties(vk.phys_device, &queue_family_property_count,
                                           queue_family_properties.data());

  vk.graphics_queue.family_index = kInvalidQueueFamilyIndex;
  vk.transfer_queue.family_index = kInvalidQueueFamilyIndex;

  for (size_t i = 0; i < queue_family_properties.size(); ++i) {
    VkQueueFamilyProperties& props = queue_family_properties[i];
    if (!props.queueCount) {
      continue;
    }

    if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      if (vk.graphics_queue.family_index == kInvalidQueueFamilyIndex) {
        vk.graphics_queue.family_index = i;
      }
      continue;
    }

    if (props.queueFlags & VK_QUEUE_COMPUTE_BIT) {
      continue;
    }

    if (props.queueFlags & VK_QUEUE_TRANSFER_BIT) {
      if (vk.transfer_queue.family_index == kInvalidQueueFamilyIndex) {
        vk.transfer_queue.family_index = i;
      }
      continue;
    }
  }

  CHECK_NE(vk.graphics_queue.family_index, kInvalidQueueFamilyIndex);

  const float queue_priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  if (vk.transfer_queue.family_index == kInvalidQueueFamilyIndex) {
    queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.graphics_queue.family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    });
  } else {
    queue_create_infos.reserve(2);

    queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.graphics_queue.family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    });

    queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.transfer_queue.family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    });
  }

  auto device_extensions = std::to_array({
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  });

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
      .enabledExtensionCount = device_extensions.size(),
      .ppEnabledExtensionNames = device_extensions.data(),
  };
  VK_CHECK(vkCreateDevice(vk.phys_device, &device_create_info, nullptr, &vk.dev));

  vkGetDeviceQueue(vk.dev, vk.graphics_queue.family_index, 0, &vk.graphics_queue.queue);
  if (vk.transfer_queue.family_index == kInvalidQueueFamilyIndex) {
    vk.transfer_queue.family_index = vk.graphics_queue.family_index;
    vk.transfer_queue.queue = vk.graphics_queue.queue;
  } else {
    vkGetDeviceQueue(vk.dev, vk.transfer_queue.family_index, 0, &vk.transfer_queue.queue);
  }

  auto init_queue = [](VkQueueState& queue, uint32_t num_command_buffers) {
    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue.family_index,
    };
    VK_CHECK(vkCreateCommandPool(vk.dev, &command_pool_create_info, nullptr, &queue.cmd_pool));

    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = queue.cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = num_command_buffers,
    };
    VK_CHECK(vkAllocateCommandBuffers(vk.dev, &alloc_info, queue.cmd));

    queue.timeline = VkCreateTimelineSemaphore(0);
    queue.timeline_next = 1;
  };
  init_queue(vk.graphics_queue, vk.frames_inflight);
  init_queue(vk.transfer_queue, 1);

  vk.acquire_semaphores.resize(vk.frames_inflight);
  for (size_t i = 0; i < vk.frames_inflight; ++i) {
    vk.acquire_semaphores[i] = VkCreateSemaphore();
  }

  Vulkan_PlatformSetSurface();
  CreateSwapchain();
}

static void CreateSwapchain() {
}

VkPipeline Vulkan_CreateGraphicsPipeline(const VkPipelineVertexInputStateCreateInfo& vertex_input_create_info,
                                         VkPipelineLayout pipeline_layout,
                                         std::span<const VkPipelineShaderStageCreateInfo> stages,
                                         VkPipelineRasterizationStateCreateInfo rasterizer_create_info) {
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

  const VkGraphicsPipelineCreateInfo pipeline_create_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pipeline_rendering_create_info,
      .stageCount = static_cast<uint32_t>(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = &vertex_input_create_info,
      .pInputAssemblyState = &input_assembly_create_info,
      .pViewportState = &viewport_state_create_info,
      .pRasterizationState = &rasterizer_create_info,
      .pMultisampleState = &multisampling_create_info,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_blending_create_info,
      .pDynamicState = &dynamic_state_create_info,
      .layout = pipeline_layout,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = -1,
  };

  VkPipeline graphics_pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &graphics_pipeline));
  return graphics_pipeline;
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

#if 0
#define RENDERDOC_CAPTURE_EVERY_FRAME
#endif

void Vulkan_FrameBegin() {
#if defined(RENDERDOC_CAPTURE_EVERY_FRAME)
  RenderDocCaptureStart();
#endif

  VulkWaitTimeline(vk.graphics_queue.timeline, vk.graphics_queue.cmd_done[vk.cur.iframe]);

  const VkSemaphore acquire_semaphore = vk.acquire_semaphores[vk.cur.iframe];
  VkResult acquire_result = vkAcquireNextImageKHR(vk.dev, vk.swapchain, std::numeric_limits<uint64_t>::max(),
                                                  acquire_semaphore, VK_NULL_HANDLE, &vk.cur.swapchain_image_index);

  if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_result == VK_SUBOPTIMAL_KHR) {
    CreateSwapchain();
    Vulkan_FrameBegin();
    return;
  }
  VK_CHECK(acquire_result);

  static uint64_t last = GetMonotonicTimeNanos();
  const uint64_t now = GetMonotonicTimeNanos();
  const uint64_t dtnanos = now - last;
  last = now;

  vk.cur.dt = dtnanos / 1e9;

  static float dtnanosaccum = .0f;
  dtnanosaccum += dtnanos;

  static uint32_t fps_frames = 0;
  ++fps_frames;

  if (dtnanosaccum >= .5f * 1e9) {
    fps = (1e9 / dtnanosaccum) * fps_frames;

    fps_frames = 0;
    dtnanosaccum = .0f;
  }

  const VkCommandBuffer cmd = vk.graphics_queue.cmd[vk.cur.iframe];
  VK_CHECK(vkResetCommandBuffer(cmd, 0));

  const VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  VK_CHECK(vkBeginCommandBuffer(cmd, &command_buffer_begin_info));
}

void Vulkan_RenderingBegin() {
  const VkCommandBuffer cmd = vk.graphics_queue.cmd[vk.cur.iframe];

  {
    const VkImageMemoryBarrier image_memory_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,  // TODO:
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = vk.swapchain_images[vk.cur.swapchain_image_index],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &image_memory_barrier);
  }

  const VkRenderingAttachmentInfo color_attachment_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = vk.swapchain_image_views[vk.cur.swapchain_image_index],
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
  };

  const VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, vk.surface_extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_info,
  };

  vkCmdBeginRendering(cmd, &rendering_info);
  {
    const VkViewport viewport{
        .x = 0.f,
        .y = static_cast<float>(vk.surface_extent.height),
        .width = static_cast<float>(vk.surface_extent.width),
        .height = -static_cast<float>(vk.surface_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = vk.surface_extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
  }
}

void Vulkan_RenderingEnd() {
  const VkCommandBuffer cmd = vk.graphics_queue.cmd[vk.cur.iframe];

  vkCmdEndRendering(cmd);

  {
    const VkImageMemoryBarrier image_memory_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = vk.swapchain_images[vk.cur.swapchain_image_index],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &image_memory_barrier);
  }

  CHECK_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
}

void Vulkan_FrameEnd() {
  const VkCommandBuffer cmd = vk.graphics_queue.cmd[vk.cur.iframe];

  const VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  const VkSemaphore acquire_semaphore = vk.acquire_semaphores[vk.cur.iframe];
  const VkTimelineSemaphoreSubmitInfo timeline_submit_info{
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .signalSemaphoreValueCount = 1,
      .pSignalSemaphoreValues = &(vk.graphics_queue.cmd_done[vk.cur.iframe] = vk.graphics_queue.timeline_next++),
  };
  const VkSubmitInfo submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = &timeline_submit_info,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &acquire_semaphore,
      .pWaitDstStageMask = wait_stages,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &vk.graphics_queue.timeline,
  };
  VK_CHECK(vkQueueSubmit(vk.graphics_queue.queue, 1, &submit_info, VK_NULL_HANDLE));

  const VkTimelineSemaphoreSubmitInfo timeline_present_info{
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .waitSemaphoreValueCount = 1,
      .pWaitSemaphoreValues = &vk.graphics_queue.cmd_done[vk.cur.iframe],
  };
  const VkPresentInfoKHR present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = &timeline_present_info,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &vk.graphics_queue.timeline,
      .swapchainCount = 1,
      .pSwapchains = &vk.swapchain,
      .pImageIndices = &vk.cur.swapchain_image_index,
  };
  const VkResult present_result = vkQueuePresentKHR(vk.graphics_queue.queue, &present_info);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
    CreateSwapchain();
  } else {
    VK_CHECK(present_result);
  }

  vk.cur.iframe = (vk.cur.iframe + 1) % vk.frames_inflight;

#if defined(RENDERDOC_CAPTURE_EVERY_FRAME)
  RenderDocCaptureEnd();
#endif
}

void VulkanNameHandle(void* object_handle, const std::string& name) {
#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)
  static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT =
      VK_GET_INSTANCE_PROC_ADDR(vkSetDebugUtilsObjectNameEXT);

  const VkDebugUtilsObjectNameInfoEXT name_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_BUFFER,
      .objectHandle = reinterpret_cast<uint64_t>(object_handle),
      .pObjectName = name.c_str(),
  };
  vkSetDebugUtilsObjectNameEXT(vk.dev, &name_info);
#endif
}

VkSemaphore VkCreateTimelineSemaphore(uint64_t initial_value) {
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

void VulkWaitTimeline(VkSemaphore timeline, uint64_t wait_value) {
  const VkSemaphoreWaitInfo wait_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .pNext = NULL,
      .flags = 0,
      .semaphoreCount = 1,
      .pSemaphores = &timeline,
      .pValues = &wait_value,
  };
  vkWaitSemaphores(vk.dev, &wait_info, std::numeric_limits<uint64_t>::max());
}

VkSemaphore VkCreateSemaphore() {
  const VkSemaphoreCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  VkSemaphore semaphore;
  VK_CHECK(vkCreateSemaphore(vk.dev, &create_info, nullptr, &semaphore));
  return semaphore;
}

VkFence VkCreateFence(bool signaled) {
  VkFenceCreateFlags flags{};
  if (signaled) flags += VK_FENCE_CREATE_SIGNALED_BIT;

  const VkFenceCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = flags,
  };

  VkFence fence;
  VK_CHECK(vkCreateFence(vk.dev, &create_info, nullptr, &fence));
  return fence;
}

#if !defined(NDEBUG) && !defined(NYLA_VULKAN_NDEBUG)

static VkBool32 DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
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

#endif

}  // namespace nyla