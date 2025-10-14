#include "nyla/vulkan/vulkan.h"

#define VK_USE_PLATFORM_XCB_KHR

#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "vulkan/vulkan.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {}  // namespace nyla

#define GET_INSTANCE_PROC_ADDR(name) \
  reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))

struct Vec2 {
  union {
    float data[2];
    struct {
      float x;
      float y;
    };
  };

  constexpr Vec2(float x, float y) : x{x}, y{y} {};
};

struct Vec3 {
  union {
    float data[3];
    struct {
      float x;
      float y;
      float z;
    };
    struct {
      float r;
      float g;
      float b;
    };
  };

  constexpr Vec3(float x, float y, float z) : x{x}, y{y}, z{z} {};
};

struct Vertex {
  Vec2 pos;
  Vec3 color;
};

static std::vector<char> ReadFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  CHECK(file.is_open());

  std::vector<char> buffer(file.tellg());

  file.seekg(0);
  file.read(buffer.data(), buffer.size());

  file.close();
  return buffer;
}

static VkDevice device;
static VkPhysicalDevice phys_device;

static VkSemaphore CreateSemaphore() {
  const VkSemaphoreCreateInfo semaphore_create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  VkSemaphore semaphore;
  CHECK_EQ(
      vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore),
      VK_SUCCESS);
  return semaphore;
}

static VkFence CreateFence(bool signaled = false) {
  VkFenceCreateFlags flags{};
  if (signaled) flags += VK_FENCE_CREATE_SIGNALED_BIT;

  const VkFenceCreateInfo fence_create_info{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = flags,
  };

  VkFence fence;
  CHECK_EQ(vkCreateFence(device, &fence_create_info, nullptr, &fence),
           VK_SUCCESS);
  return fence;
}

static VkShaderModule CreateShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo shader_module_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };

  VkShaderModule shader_module;
  CHECK(vkCreateShaderModule(device, &shader_module_info, nullptr,
                             &shader_module) == VK_SUCCESS);
  return shader_module;
}

static std::pair<VkBuffer, VkDeviceMemory> CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) {
  const VkBufferCreateInfo buffer_create_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VkBuffer buffer;
  CHECK_EQ(vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer),
           VK_SUCCESS);

  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

  const VkMemoryAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_requirements.size,
      .memoryTypeIndex =
          [=]() {
            VkPhysicalDeviceMemoryProperties mem_propertities;
            vkGetPhysicalDeviceMemoryProperties(phys_device, &mem_propertities);

            for (uint32_t i = 0; i < mem_propertities.memoryTypeCount; ++i) {
              if (!(mem_requirements.memoryTypeBits & (1 << i))) {
                continue;
              }
              if ((mem_propertities.memoryTypes[i].propertyFlags &
                   properties) != properties) {
                continue;
              }
              return i;
            }

            CHECK(false);
          }(),
  };

  VkDeviceMemory buffer_memory;
  CHECK_EQ(vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory),
           VK_SUCCESS);
  CHECK_EQ(vkBindBufferMemory(device, buffer, buffer_memory, 0), VK_SUCCESS);

  return {buffer, buffer_memory};
}

static std::pair<VkBuffer, VkDeviceMemory> CreateBuffer(
    VkCommandPool command_pool, VkQueue transfer_queue, VkDeviceSize data_size,
    const void* src_data, VkBufferUsageFlags usage) {
  auto [staging_buffer, staging_buffer_memory] =
      CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  void* dst_data;
  vkMapMemory(device, staging_buffer_memory, 0, data_size, 0, &dst_data);
  memcpy(dst_data, src_data, data_size);
  vkUnmapMemory(device, staging_buffer_memory);

  auto [buffer, buffer_memory] =
      CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  const VkCommandBufferAllocateInfo command_buffer_alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer command_buffer;
  CHECK_EQ(vkAllocateCommandBuffers(device, &command_buffer_alloc_info,
                                    &command_buffer),
           VK_SUCCESS);

  const VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };

  CHECK_EQ(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info),
           VK_SUCCESS);

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
  vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
  vkDestroyBuffer(device, staging_buffer, nullptr);
  vkFreeMemory(device, staging_buffer_memory, nullptr);

  return {buffer, buffer_memory};
}

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  int iscreen;
  xcb_connection_t* conn = xcb_connect(nullptr, &iscreen);
  xcb_screen_t* screen = xcb_aux_get_screen(conn, iscreen);

  xcb_window_t window = xcb_generate_id(conn);

  CHECK(!xcb_request_check(
      conn, xcb_create_window_checked(conn, XCB_COPY_FROM_PARENT, window,
                                      screen->root, 0, 0, 100, 100, 0,
                                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                      screen->root_visual, 0, nullptr)));

  xcb_map_window(conn, window);
  xcb_flush(conn);

  //

  VkInstance instance = []() {
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "nyla",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "nyla",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    auto instance_extensions = std::to_array({
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    });

    auto validation_layers = std::to_array({
        "VK_LAYER_KHRONOS_validation",
    });

#ifndef NDEBUG
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback =
            [](VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
               VkDebugUtilsMessageTypeFlagsEXT message_type,
               const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
               void* user_data) {
              LOG(ERROR) << "\n" << callback_data->pMessage << "\n";
              return VK_FALSE;
            },
    };
#endif

    VkInstanceCreateInfo instance_create_info{
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
    CHECK_EQ(vkCreateInstance(&instance_create_info, nullptr, &instance),
             VK_SUCCESS);

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
    CHECK_EQ(
        GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT)(
            instance, &debug_messenger_create_info, nullptr, &debug_messenger),
        VK_SUCCESS);
#endif

    return instance;
  }();

  phys_device = [=]() {
    uint32_t num_phys_devices = 0;
    CHECK_EQ(vkEnumeratePhysicalDevices(instance, &num_phys_devices, nullptr),
             VK_SUCCESS);

    std::vector<VkPhysicalDevice> phys_devices(num_phys_devices);
    CHECK_EQ(vkEnumeratePhysicalDevices(instance, &num_phys_devices,
                                        phys_devices.data()),
             VK_SUCCESS);

    return phys_devices.front();
  }();

  uint32_t queue_family_property_count = 1;
  VkQueueFamilyProperties queue_family_property;
  vkGetPhysicalDeviceQueueFamilyProperties(
      phys_device, &queue_family_property_count, &queue_family_property);

  CHECK(queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  CHECK(queue_family_property.queueFlags & VK_QUEUE_COMPUTE_BIT);
  CHECK(queue_family_property.queueFlags & VK_QUEUE_TRANSFER_BIT);

  uint32_t queue_family_index = 0;

  device = [=]() {
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
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &v13,
    };

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &features;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.enabledExtensionCount = device_extensions.size();
    device_create_info.ppEnabledExtensionNames = device_extensions.data();

    VkDevice device;
    CHECK_EQ(vkCreateDevice(phys_device, &device_create_info, nullptr, &device),
             VK_SUCCESS);
    return device;
  }();

  VkQueue queue;
  vkGetDeviceQueue(device, queue_family_index, 0, &queue);

  VkSurfaceKHR surface = [=]() {
    VkXcbSurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    create_info.connection = conn;
    create_info.window = window;

    VkSurfaceKHR surface;
    CHECK_EQ(vkCreateXcbSurfaceKHR(instance, &create_info, nullptr, &surface),
             VK_SUCCESS);
    return surface;
  }();

  VkSurfaceCapabilitiesKHR surface_capabilities{};
  CHECK_EQ(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface,
                                                     &surface_capabilities),
           VK_SUCCESS);

  VkSurfaceFormatKHR surface_format = [=]() {
    std::vector<VkSurfaceFormatKHR> surface_formats;
    uint32_t surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface,
                                         &surface_format_count, nullptr);
    CHECK(surface_format_count);

    surface_formats.resize(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        phys_device, surface, &surface_format_count, surface_formats.data());

    auto it = std::find_if(
        surface_formats.begin(), surface_formats.end(),
        [](VkSurfaceFormatKHR surface_format) {
          return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                 surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    CHECK(it != surface_formats.end());

    return *it;
  }();

  VkPresentModeKHR present_mode = [=]() {
    std::vector<VkPresentModeKHR> present_modes;
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device, surface,
                                              &present_mode_count, nullptr);
    CHECK(present_mode_count);

    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        phys_device, surface, &present_mode_count, present_modes.data());

    return present_modes.front();
  }();

  VkExtent2D surface_extent = [=]() {
    if (surface_capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
      return surface_capabilities.currentExtent;
    }

    xcb_get_geometry_reply_t* window_geometry =
        xcb_get_geometry_reply(conn, xcb_get_geometry(conn, window), nullptr);
    CHECK(window_geometry);

    VkExtent2D surface_extent{};
    surface_extent.width =
        std::clamp(static_cast<uint32_t>(window_geometry->width),
                   surface_capabilities.minImageExtent.width,
                   surface_capabilities.maxImageExtent.width);
    surface_extent.height =
        std::clamp(static_cast<uint32_t>(window_geometry->height),
                   surface_capabilities.minImageExtent.height,
                   surface_capabilities.maxImageExtent.height);

    return surface_extent;
  }();

  uint32_t image_count = surface_capabilities.minImageCount + 1;
  if (surface_capabilities.maxImageCount)
    image_count = std::min(surface_capabilities.maxImageCount, image_count);

  VkSwapchainKHR swapchain = [=]() {
    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = surface_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = surface_capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain{};
    CHECK_EQ(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain),
             VK_SUCCESS);
    return swapchain;
  }();

  std::vector<VkImage> swapchain_images = [=]() {
    uint32_t swapchain_image_count;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);

    std::vector<VkImage> swapchain_images(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count,
                            swapchain_images.data());
    return swapchain_images;
  }();

  std::vector<VkImageView> swapchain_image_views(swapchain_images.size());

  for (size_t iimage = 0; iimage < swapchain_images.size(); ++iimage) {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    create_info.image = swapchain_images.at(iimage),
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D,
    create_info.format = surface_format.format,
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    CHECK_EQ(vkCreateImageView(device, &create_info, nullptr,
                               &swapchain_image_views.at(iimage)),
             VK_SUCCESS);
  }

  const VkPipeline graphics_pipeline = [=]() {
    const auto shader_stages = std::to_array<VkPipelineShaderStageCreateInfo>({
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module =
                CreateShaderModule(ReadFile("nyla/vulkan/shaders/vert.spv")),
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module =
                CreateShaderModule(ReadFile("nyla/vulkan/shaders/frag.spv")),
            .pName = "main",
        },
    });

    const VkPipelineLayout pipeline_layout = [=]() {
      const VkDescriptorSetLayoutBinding ubo_layout_binding{
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      };

      const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 1,
          .pBindings = &ubo_layout_binding,
      };

      VkDescriptorSetLayout descriptor_set_layout;
      CHECK_EQ(vkCreateDescriptorSetLayout(device,
                                           &descriptor_set_layout_create_info,
                                           nullptr, &descriptor_set_layout),
               VK_SUCCESS);

      const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 1,
          .pSetLayouts = &descriptor_set_layout,
      };

      VkPipelineLayout pipeline_layout;
      vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                             &pipeline_layout);
      return pipeline_layout;
    }();

    const VkPipelineRenderingCreateInfo pipeline_rendering_create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &surface_format.format,
    };

    const VkVertexInputBindingDescription binding_description{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    const VkVertexInputAttributeDescription attr_description[2] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 0,
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color),
        },
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attr_description,
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

    const VkPipelineRasterizationStateCreateInfo rasterizer_create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
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
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
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

    const VkGraphicsPipelineCreateInfo pipeline_create_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipeline_rendering_create_info,
        .stageCount = shader_stages.size(),
        .pStages = shader_stages.data(),
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
    CHECK_EQ(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                       &pipeline_create_info, nullptr,
                                       &graphics_pipeline),
             VK_SUCCESS);
    return graphics_pipeline;
  }();

  const VkCommandPoolCreateInfo command_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family_index,
  };

  VkCommandPool command_pool;
  CHECK(vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                            &command_pool) == VK_SUCCESS);

  const std::vector<Vertex> vertices = {
      {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
      {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
      {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
  };

  auto [vertex_buffer, vertex_buffer_memory] =
      CreateBuffer(command_pool, queue, sizeof(vertices[0]) * vertices.size(),
                   vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  const uint8_t num_in_flight_frames = 2;

  std::vector<VkCommandBuffer> command_buffers(num_in_flight_frames);
  for (uint8_t i = 0; i < num_in_flight_frames; ++i) {
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    CHECK_EQ(vkAllocateCommandBuffers(device, &alloc_info,
                                      command_buffers.data() + i),
             VK_SUCCESS);
  }

  std::vector<VkSemaphore> acquire_semaphores(num_in_flight_frames);
  std::vector<VkFence> frame_fences(num_in_flight_frames);

  for (uint8_t i = 0; i < num_in_flight_frames; ++i) {
    acquire_semaphores[i] = CreateSemaphore();
    frame_fences[i] = CreateFence(true);
  }

  std::vector<VkSemaphore> submit_semaphores(swapchain_images.size());
  for (uint8_t i = 0; i < swapchain_images.size(); ++i) {
    submit_semaphores[i] = CreateSemaphore();
  }

  for (uint8_t iframe = 0; iframe < num_in_flight_frames;
       iframe = ((iframe + 1) % num_in_flight_frames)) {
    const VkFence frame_fence = frame_fences[iframe];

    vkWaitForFences(device, 1, &frame_fence, VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(device, 1, &frame_fence);

    const VkSemaphore acquire_semaphore = acquire_semaphores[iframe];
    uint32_t image_index;
    VkResult acquire_result = vkAcquireNextImageKHR(
        device, swapchain, std::numeric_limits<uint64_t>::max(),
        acquire_semaphore, VK_NULL_HANDLE, &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) break;
    CHECK((acquire_result == VK_SUBOPTIMAL_KHR) ||
          (acquire_result == VK_SUCCESS));

    const VkCommandBuffer command_buffer = command_buffers[iframe];

    CHECK_EQ(vkResetCommandBuffer(command_buffer, 0), VK_SUCCESS);

    const VkCommandBufferBeginInfo command_buffer_begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) ==
          VK_SUCCESS);

    const VkRenderingAttachmentInfo color_attachment_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
    };

    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, surface_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_info,
    };

    {
      static uint32_t first_frame = 0;
      const uint32_t image_bit = 1 << image_index;
      const VkImageLayout old_layout = (first_frame & image_bit)
                                           ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                           : VK_IMAGE_LAYOUT_UNDEFINED;
      first_frame |= image_bit;

      const VkImageMemoryBarrier image_memory_barrier{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = old_layout,
          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .image = swapchain_images[image_index],
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
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &image_memory_barrier);
    }

    vkCmdBeginRendering(command_buffer, &rendering_info);
    {
      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        graphics_pipeline);

      VkBuffer vertex_buffers[] = {vertex_buffer};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

      VkViewport viewport{};
      viewport.x = 0.0f;
      viewport.y = 0.0f;
      viewport.width = static_cast<float>(surface_extent.width);
      viewport.height = static_cast<float>(surface_extent.height);
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;

      vkCmdSetViewport(command_buffer, 0, 1, &viewport);

      VkRect2D scissor{};
      scissor.offset = {0, 0};
      scissor.extent = surface_extent;

      vkCmdSetScissor(command_buffer, 0, 1, &scissor);

      vkCmdDraw(command_buffer, vertices.size(), 1, 0, 0);
    }
    vkCmdEndRendering(command_buffer);

    {
      const VkImageMemoryBarrier image_memory_barrier{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .image = swapchain_images[image_index],
          .subresourceRange =
              {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
      };

      vkCmdPipelineBarrier(command_buffer,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &image_memory_barrier);
    }

    CHECK_EQ(vkEndCommandBuffer(command_buffer), VK_SUCCESS);

    const VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSemaphore submit_semaphore = submit_semaphores[image_index];
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

    CHECK_EQ(vkQueueSubmit(queue, 1, &submit_info, frame_fence), VK_SUCCESS);

    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &submit_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    vkQueuePresentKHR(queue, &present_info);
  }
}
