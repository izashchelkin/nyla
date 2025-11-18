#include "nyla/vulkan/vulkan.h"

#include <sys/inotify.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/commons/os/clock.h"

namespace nyla {

static void CreateSwapchain();

Vulkan_State vk;

void Vulkan_Initialize(std::span<const char* const> shader_watch_directories) {
  vk.instance = []() {
    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "nyla",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "nyla",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    const auto instance_extensions = std::to_array({
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    });

    const auto validation_layers = std::to_array({
        "VK_LAYER_KHRONOS_validation",
    });

#ifndef NDEBUG
    const VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback =
            [](VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
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
            },
    };
#endif

    const VkInstanceCreateInfo instance_create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifndef NDEBUG
        .pNext = &debug_messenger_create_info,
#endif
        .pApplicationInfo = &app_info,
        .enabledLayerCount = validation_layers.size(),
        .ppEnabledLayerNames = validation_layers.data(),
        .enabledExtensionCount = instance_extensions.size(),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    VkInstance instance;
    CHECK_EQ(vkCreateInstance(&instance_create_info, nullptr, &instance), VK_SUCCESS);

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
    CHECK_EQ(VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT)(instance, &debug_messenger_create_info, nullptr,
                                                                       &debug_messenger),
             VK_SUCCESS);
#endif

    return instance;
  }();

  vk.phys_device = [=]() {
    uint32_t num_phys_devices = 0;
    CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, nullptr), VK_SUCCESS);

    std::vector<VkPhysicalDevice> phys_devices(num_phys_devices);
    CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, phys_devices.data()), VK_SUCCESS);

    return phys_devices.front();
  }();

  vkGetPhysicalDeviceProperties(vk.phys_device, &vk.phys_device_props);

  uint32_t queue_family_property_count = 1;
  VkQueueFamilyProperties queue_family_property;
  vkGetPhysicalDeviceQueueFamilyProperties(vk.phys_device, &queue_family_property_count, &queue_family_property);

  CHECK(queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  CHECK(queue_family_property.queueFlags & VK_QUEUE_COMPUTE_BIT);
  CHECK(queue_family_property.queueFlags & VK_QUEUE_TRANSFER_BIT);

  uint32_t queue_family_index = 0;

  {
    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    auto device_extensions = std::to_array({
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    });

    VkPhysicalDeviceVulkan13Features v13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features v12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &v13,
        .scalarBlockLayout = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &v12,
    };

    const VkDeviceCreateInfo device_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = device_extensions.size(),
        .ppEnabledExtensionNames = device_extensions.data(),
    };
    VK_CHECK(vkCreateDevice(vk.phys_device, &device_create_info, nullptr, &vk.device));
  }

  vkGetDeviceQueue(vk.device, queue_family_index, 0, &vk.queue);

  Vulkan_PlatformSetSurface();
  CreateSwapchain();

  const VkCommandPoolCreateInfo command_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = vk.queue_family_index,
  };

  CHECK(vkCreateCommandPool(vk.device, &command_pool_create_info, nullptr, &vk.command_pool) == VK_SUCCESS);

  vk.command_buffers.resize(kVulkan_NumFramesInFlight);
  for (uint8_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VK_CHECK(vkAllocateCommandBuffers(vk.device, &alloc_info, vk.command_buffers.data() + i));
  }

  vk.acquire_semaphores.resize(kVulkan_NumFramesInFlight);
  vk.frame_fences.resize(kVulkan_NumFramesInFlight);
  for (uint8_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
    vk.acquire_semaphores[i] = CreateSemaphore();
    vk.frame_fences[i] = CreateFence(true);
  }

  vk.submit_semaphores.resize(vk.swapchain_image_count());
  for (uint8_t i = 0; i < vk.swapchain_image_count(); ++i) {
    vk.submit_semaphores[i] = CreateSemaphore();
  }

  //

  {
    vk.shaderdir_inotify_fd = inotify_init1(IN_NONBLOCK);
    CHECK(vk.shaderdir_inotify_fd > 0);

    for (const char* dir : shader_watch_directories) {
      CHECK_GT(inotify_add_watch(vk.shaderdir_inotify_fd, dir, IN_MODIFY), 0);
    }
  }
}

static void CreateSwapchain() {
  VkSwapchainKHR old_swapchain = vk.swapchain;
  std::vector<VkImageView> old_image_views = vk.swapchain_image_views;

  {
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys_device, vk.surface, &vk.surface_capabilities));
  }

  {
    uint32_t surface_format_count;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_device, vk.surface, &surface_format_count, nullptr));

    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys_device, vk.surface, &surface_format_count, surface_formats.data());

    auto it = std::find_if(surface_formats.begin(), surface_formats.end(), [](VkSurfaceFormatKHR surface_format) {
      return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
             surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    CHECK(it != surface_formats.end());

    vk.surface_format = *it;
  }

  {
    std::vector<VkPresentModeKHR> present_modes;
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_device, vk.surface, &present_mode_count, nullptr);
    CHECK(present_mode_count);

    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.phys_device, vk.surface, &present_mode_count, present_modes.data());

    vk.present_mode = VK_PRESENT_MODE_FIFO_KHR;
  }

  if (vk.surface_capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
    vk.surface_extent = Vulkan_PlatformGetWindowExtent();

    vk.surface_extent.width = std::clamp(vk.surface_extent.width, vk.surface_capabilities.minImageExtent.width,
                                         vk.surface_capabilities.maxImageExtent.width),
    vk.surface_extent.height = std::clamp(vk.surface_extent.height, vk.surface_capabilities.minImageExtent.height,
                                          vk.surface_capabilities.maxImageExtent.height);
  } else {
    vk.surface_extent = vk.surface_capabilities.currentExtent;
  }

  {
    uint32_t min_image_count = vk.surface_capabilities.minImageCount + 1;
    if (vk.surface_capabilities.maxImageCount) {
      min_image_count = std::min(vk.surface_capabilities.maxImageCount, min_image_count);
    }

    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = min_image_count,
        .imageFormat = vk.surface_format.format,
        .imageColorSpace = vk.surface_format.colorSpace,
        .imageExtent = vk.surface_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = vk.surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = vk.present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = vk.swapchain,
    };
    VK_CHECK(vkCreateSwapchainKHR(vk.device, &create_info, nullptr, &vk.swapchain));
  }

  {
    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swapchain_image_count, nullptr);

    vk.swapchain_images.resize(swapchain_image_count);
    vk.swapchain_image_views.resize(swapchain_image_count);

    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swapchain_image_count, vk.swapchain_images.data());

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

      VK_CHECK(vkCreateImageView(vk.device, &create_info, nullptr, &vk.swapchain_image_views[i]));
    }
  }

  if (old_swapchain) {
    vkDeviceWaitIdle(vk.device);

    for (const VkImageView image_view : old_image_views) vkDestroyImageView(vk.device, image_view, nullptr);

    vkDestroySwapchainKHR(vk.device, old_swapchain, nullptr);
  }
}

VkPipeline Vulkan_CreateGraphicsPipeline(const VkPipelineVertexInputStateCreateInfo& vertex_input_create_info,
                                         VkPipelineLayout pipeline_layout,
                                         std::span<const VkPipelineShaderStageCreateInfo> stages) {
  const VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  const VkPipelineViewportStateCreateInfo viewport_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };

  const VkPipelineRasterizationStateCreateInfo rasterizer_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
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
  VK_CHECK(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &graphics_pipeline));
  return graphics_pipeline;
}

VkShaderModule Vulkan_CreateShaderModule(const std::vector<char>& code) {
  const VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };

  VkShaderModule shader_module;
  VK_CHECK(vkCreateShaderModule(vk.device, &create_info, nullptr, &shader_module));
  return shader_module;
}

void Vulkan_FrameBegin() {
  const VkFence frame_fence = vk.frame_fences[vk.current_frame_data.iframe];

  vkWaitForFences(vk.device, 1, &frame_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
  vkResetFences(vk.device, 1, &frame_fence);

  {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    char* bufp = buf;

    int numread;

    while ((numread = read(vk.shaderdir_inotify_fd, buf, sizeof(buf))) > 0) {
      while (bufp != buf + numread) {
        inotify_event* event = reinterpret_cast<inotify_event*>(bufp);
        bufp += sizeof(inotify_event);

        if (event->mask & IN_ISDIR) {
          bufp += event->len;
        } else {
          std::string_view name = {bufp, strlen(bufp)};
          bufp += event->len;

          if (name.ends_with(".spv")) {
            vk.shaders_invalidated = true;
            continue;
          }

          if (name.ends_with(".vert") || name.ends_with(".frag")) {
            vk.shaders_recompile = true;
          }
        }
      }
    }

    if (vk.shaders_recompile) {
      vk.shaders_invalidated = false;

      LOG(INFO) << "shaders recompiling";
      system("python3 scripts/shaders.py");
      vk.shaders_recompile = false;
    }

    if (vk.shaders_invalidated) {
      LOG(INFO) << "shaders invalidated";
    }
  }

  const VkSemaphore acquire_semaphore = vk.acquire_semaphores[vk.current_frame_data.iframe];
  VkResult acquire_result =
      vkAcquireNextImageKHR(vk.device, vk.swapchain, std::numeric_limits<uint64_t>::max(), acquire_semaphore,
                            VK_NULL_HANDLE, &vk.current_frame_data.swapchain_image_index);

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

  vk.current_frame_data.dt = dtnanos / (float)1e9;

  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];

  VK_CHECK(vkResetCommandBuffer(command_buffer, 0));

  const VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  VK_CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));
}

void Vulkan_RenderingBegin() {
  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];

  {
    const VkImageMemoryBarrier image_memory_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,  // TODO:
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = vk.swapchain_images[vk.current_frame_data.swapchain_image_index],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &image_memory_barrier);
  }

  const VkRenderingAttachmentInfo color_attachment_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = vk.swapchain_image_views[vk.current_frame_data.swapchain_image_index],
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

  vkCmdBeginRendering(command_buffer, &rendering_info);
  {
    const VkViewport viewport{
        .x = 0.f,
        .y = static_cast<float>(vk.surface_extent.height),
        .width = static_cast<float>(vk.surface_extent.width),
        .height = -static_cast<float>(vk.surface_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = vk.surface_extent,
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
  }
}

void Vulkan_RenderingEnd() {
  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];

  vkCmdEndRendering(command_buffer);

  {
    const VkImageMemoryBarrier image_memory_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = vk.swapchain_images[vk.current_frame_data.swapchain_image_index],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
  }

  CHECK_EQ(vkEndCommandBuffer(command_buffer), VK_SUCCESS);
}

void Vulkan_FrameEnd() {
  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];

  const VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  const VkSemaphore acquire_semaphore = vk.acquire_semaphores[vk.current_frame_data.iframe];
  const VkSemaphore submit_semaphore = vk.submit_semaphores[vk.current_frame_data.swapchain_image_index];
  const VkSubmitInfo submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &acquire_semaphore,
      .pWaitDstStageMask = wait_stages,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &submit_semaphore,
  };

  const VkFence frame_fence = vk.frame_fences[vk.current_frame_data.iframe];
  VK_CHECK(vkQueueSubmit(vk.queue, 1, &submit_info, frame_fence));

  const VkPresentInfoKHR present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &submit_semaphore,
      .swapchainCount = 1,
      .pSwapchains = &vk.swapchain,
      .pImageIndices = &vk.current_frame_data.swapchain_image_index,
  };

  VkResult present_result = vkQueuePresentKHR(vk.queue, &present_info);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
    CreateSwapchain();
  } else {
    VK_CHECK(present_result);
  }

  vk.current_frame_data.iframe = (vk.current_frame_data.iframe + 1) % kVulkan_NumFramesInFlight;
}

void Vulkan_CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                         VkBuffer& buffer, VkDeviceMemory& buffer_memory) {
  const VkBufferCreateInfo buffer_create_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VK_CHECK(vkCreateBuffer(vk.device, &buffer_create_info, nullptr, &buffer));

  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(vk.device, buffer, &mem_requirements);

  const VkMemoryAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_requirements.size,
      .memoryTypeIndex =
          [=]() {
            VkPhysicalDeviceMemoryProperties mem_propertities;
            vkGetPhysicalDeviceMemoryProperties(vk.phys_device, &mem_propertities);

            for (uint32_t i = 0; i < mem_propertities.memoryTypeCount; ++i) {
              if (!(mem_requirements.memoryTypeBits & (1 << i))) {
                continue;
              }
              if ((mem_propertities.memoryTypes[i].propertyFlags & properties) != properties) {
                continue;
              }
              return i;
            }

            CHECK(false);
          }(),
  };

  VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &buffer_memory));
  VK_CHECK(vkBindBufferMemory(vk.device, buffer, buffer_memory, 0));
}

void Vulkan_CreateBuffer(VkCommandPool command_pool, VkQueue transfer_queue, VkDeviceSize data_size,
                         const void* src_data, VkBufferUsageFlags usage, VkBuffer& buffer,
                         VkDeviceMemory& buffer_memory) {
  VkBuffer staging_buffer;
  VkDeviceMemory staging_buffer_memory;
  Vulkan_CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
                      staging_buffer_memory);

  void* dst_data;
  vkMapMemory(vk.device, staging_buffer_memory, 0, data_size, 0, &dst_data);
  memcpy(dst_data, src_data, data_size);
  vkUnmapMemory(vk.device, staging_buffer_memory);

  Vulkan_CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer,
                      buffer_memory);

  const VkCommandBufferAllocateInfo command_buffer_alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer command_buffer;
  VK_CHECK(vkAllocateCommandBuffers(vk.device, &command_buffer_alloc_info, &command_buffer));

  const VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };

  VK_CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

  const VkBufferCopy copy_region{
      .srcOffset = 0,
      .dstOffset = 0,
      .size = data_size,
  };
  vkCmdCopyBuffer(command_buffer, staging_buffer, buffer, 1, &copy_region);

  vkEndCommandBuffer(command_buffer);

  const VkSubmitInfo submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
  };
  vkQueueSubmit(transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(transfer_queue);
  vkFreeCommandBuffers(vk.device, command_pool, 1, &command_buffer);
  vkDestroyBuffer(vk.device, staging_buffer, nullptr);
  vkFreeMemory(vk.device, staging_buffer_memory, nullptr);
}

}  // namespace nyla