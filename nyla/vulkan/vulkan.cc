#include "nyla/vulkan/vulkan.h"

#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/math/mat4.h"
#include "nyla/math/vec3.h"
#include "nyla/vulkan/memory.h"
#include "nyla/vulkan/pipeline.h"
#include "nyla/vulkan/swapchain.h"
#include "nyla/vulkan/sync.h"
#include "nyla/x11/x11.h"
#include "vulkan/vulkan.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

static constexpr uint8_t kInFlightFrames = 2;

struct Vertex {
  struct {
    float x;
    float y;
  } pos;
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

xcb_window_t window;
VkState vk;

VkExtent2D PlatformGetWindowSize() {
  xcb_get_geometry_reply_t* window_geometry = xcb_get_geometry_reply(
      x11.conn, xcb_get_geometry(x11.conn, window), nullptr);

  return {.width = window_geometry->width, .height = window_geometry->height};
}

static int Main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  InitializeX11();

  {
    window = xcb_generate_id(x11.conn);

    CHECK(!xcb_request_check(
        x11.conn,
        xcb_create_window_checked(
            x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0,
            x11.screen->width_in_pixels, x11.screen->height_in_pixels, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
            XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
            (uint32_t[]){false, XCB_EVENT_MASK_KEY_PRESS |
                                    XCB_EVENT_MASK_KEY_RELEASE})));

    xcb_map_window(x11.conn, window);
    xcb_flush(x11.conn);
  }

  xcb_keycode_t up_keycode;
  xcb_keycode_t left_keycode;
  xcb_keycode_t down_keycode;
  xcb_keycode_t right_keycode;

  absl::flat_hash_set<xcb_keycode_t> pressed_keys;

  {
    KeyResolver key_resolver;
    InitializeKeyResolver(key_resolver);

    up_keycode = ResolveKeyCode(key_resolver, "AD03");
    left_keycode = ResolveKeyCode(key_resolver, "AC02");
    down_keycode = ResolveKeyCode(key_resolver, "AC03");
    right_keycode = ResolveKeyCode(key_resolver, "AC04");

    FreeKeyResolver(key_resolver);
  }

  //

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
    CHECK_EQ(vkCreateInstance(&instance_create_info, nullptr, &instance),
             VK_SUCCESS);

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
    CHECK_EQ(
        VK_GET_INSTANCE_PROC_ADDR(vkCreateDebugUtilsMessengerEXT)(
            instance, &debug_messenger_create_info, nullptr, &debug_messenger),
        VK_SUCCESS);
#endif

    return instance;
  }();

  vk.phys_device = [=]() {
    uint32_t num_phys_devices = 0;
    CHECK_EQ(
        vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices, nullptr),
        VK_SUCCESS);

    std::vector<VkPhysicalDevice> phys_devices(num_phys_devices);
    CHECK_EQ(vkEnumeratePhysicalDevices(vk.instance, &num_phys_devices,
                                        phys_devices.data()),
             VK_SUCCESS);

    return phys_devices.front();
  }();

  uint32_t queue_family_property_count = 1;
  VkQueueFamilyProperties queue_family_property;
  vkGetPhysicalDeviceQueueFamilyProperties(
      vk.phys_device, &queue_family_property_count, &queue_family_property);

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
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &v13,
    };

    const VkDeviceCreateInfo device_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = device_extensions.size(),
        .ppEnabledExtensionNames = device_extensions.data(),
    };
    VK_CHECK(vkCreateDevice(vk.phys_device, &device_create_info, nullptr,
                            &vk.device));
  }

  VkQueue queue;
  vkGetDeviceQueue(vk.device, queue_family_index, 0, &queue);

  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = x11.conn,
      .window = window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr,
                                 &vk.surface));

  CreateSwapchain();

  struct UniformBufferObject {
    Mat4 model;
    Mat4 view;
    Mat4 proj;
  };

  std::vector<VkBuffer> uniform_buffers(vk.swapchain_image_count());
  std::vector<VkDeviceMemory> uniform_buffers_memory(
      vk.swapchain_image_count());
  std::vector<void*> uniform_buffers_mapped(vk.swapchain_image_count());

  for (size_t i = 0; i < vk.swapchain_image_count(); ++i) {
    CreateBuffer(sizeof(UniformBufferObject),
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 uniform_buffers[i], uniform_buffers_memory[i]);

    vkMapMemory(vk.device, uniform_buffers_memory[i], 0,
                sizeof(UniformBufferObject), 0, &uniform_buffers_mapped[i]);
  }

  const VkDescriptorPool descriptor_pool = [&] {
    const VkDescriptorPoolSize descriptor_pool_size{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = static_cast<uint32_t>(vk.swapchain_image_count()),
    };

    const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = static_cast<uint32_t>(vk.swapchain_image_count()),
        .poolSizeCount = 1,
        .pPoolSizes = &descriptor_pool_size,
    };

    VkDescriptorPool descriptor_pool;
    CHECK_EQ(vkCreateDescriptorPool(vk.device, &descriptor_pool_create_info,
                                    nullptr, &descriptor_pool),
             VK_SUCCESS);
    return descriptor_pool;
  }();

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
  CHECK_EQ(
      vkCreateDescriptorSetLayout(vk.device, &descriptor_set_layout_create_info,
                                  nullptr, &descriptor_set_layout),
      VK_SUCCESS);

  const std::vector<VkDescriptorSetLayout> layouts(vk.swapchain_image_count(),
                                                   descriptor_set_layout);

  const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
  };

  std::vector<VkDescriptorSet> descriptor_sets(vk.swapchain_image_count());

  CHECK_EQ(vkAllocateDescriptorSets(vk.device, &descriptor_set_alloc_info,
                                    descriptor_sets.data()),
           VK_SUCCESS);

  for (size_t i = 0; i < vk.swapchain_image_count(); ++i) {
    const VkDescriptorBufferInfo buffer_info{
        .buffer = uniform_buffers[i],
        .offset = 0,
        .range = sizeof(UniformBufferObject),
    };

    const VkWriteDescriptorSet descriptor_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_sets[i],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_info,
        .pTexelBufferView = nullptr,
    };

    vkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, nullptr);
  }

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  VkPipelineLayout pipeline_layout;
  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr,
                         &pipeline_layout);

  const VkPipelineRenderingCreateInfo pipeline_rendering_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &vk.surface_format.format,
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
          .format = VK_FORMAT_R32G32_SFLOAT,
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
  CHECK_EQ(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                     &pipeline_create_info, nullptr,
                                     &graphics_pipeline),
           VK_SUCCESS);

  const VkCommandPoolCreateInfo command_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family_index,
  };

  VkCommandPool command_pool;
  CHECK(vkCreateCommandPool(vk.device, &command_pool_create_info, nullptr,
                            &command_pool) == VK_SUCCESS);

  const std::vector<Vertex> vertices = {
      {{0, -25.f}, {0.0f, 1.0f, 0.0f}},
      {{-25.f, 25.f}, {1.0f, 0.0f, 0.0f}},
      {{25.f, 25.f}, {0.0f, 0.0f, 1.0f}},
  };

  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;
  CreateBuffer(command_pool, queue, sizeof(vertices[0]) * vertices.size(),
               vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               vertex_buffer, vertex_buffer_memory);

  std::vector<VkCommandBuffer> command_buffers(kInFlightFrames);
  for (uint8_t i = 0; i < kInFlightFrames; ++i) {
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VK_CHECK(vkAllocateCommandBuffers(vk.device, &alloc_info,
                                      command_buffers.data() + i));
  }

  std::vector<VkSemaphore> acquire_semaphores(kInFlightFrames);
  std::vector<VkFence> frame_fences(kInFlightFrames);

  for (uint8_t i = 0; i < kInFlightFrames; ++i) {
    acquire_semaphores[i] = CreateSemaphore();
    frame_fences[i] = CreateFence(true);
  }

  std::vector<VkSemaphore> submit_semaphores(vk.swapchain_image_count());
  for (uint8_t i = 0; i < vk.swapchain_image_count(); ++i) {
    submit_semaphores[i] = CreateSemaphore();
  }

  bool running = true;
  for (uint8_t iframe = 0; iframe < kInFlightFrames;
       iframe = ((iframe + 1) % kInFlightFrames)) {
    for (;;) {
      if (xcb_connection_has_error(x11.conn)) {
        running = false;
        break;
      }

      xcb_generic_event_t* event = xcb_poll_for_event(x11.conn);
      if (!event) break;

      absl::Cleanup event_freer = [=]() { free(event); };
      const uint8_t event_type = event->response_type & 0x7F;

      switch (event_type) {
        case XCB_KEY_PRESS: {
          auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
          pressed_keys.emplace(keypress->detail);
          break;
        }

        case XCB_KEY_RELEASE: {
          auto keyrelease = reinterpret_cast<xcb_key_release_event_t*>(event);
          pressed_keys.erase(keyrelease->detail);
          break;
        }

        case XCB_CLIENT_MESSAGE: {
          auto clientmessage =
              reinterpret_cast<xcb_client_message_event_t*>(event);
					LOG(INFO) << "here";

          if (clientmessage->format == 32 &&
              clientmessage->type == x11.atoms.wm_protocols &&
              clientmessage->data.data32[0] == x11.atoms.wm_delete_window) {
            running = false;
          }
          break;
        }
      }
    }
    if (!running) break;

    const VkFence frame_fence = frame_fences[iframe];

    vkWaitForFences(vk.device, 1, &frame_fence, VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(vk.device, 1, &frame_fence);

    const VkSemaphore acquire_semaphore = acquire_semaphores[iframe];
    uint32_t image_index;
    VkResult acquire_result = vkAcquireNextImageKHR(
        vk.device, vk.swapchain, std::numeric_limits<uint64_t>::max(),
        acquire_semaphore, VK_NULL_HANDLE, &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR ||
        acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
      CreateSwapchain();
      continue;
    }
    VK_CHECK(acquire_result);

    const VkCommandBuffer command_buffer = command_buffers[iframe];

    CHECK_EQ(vkResetCommandBuffer(command_buffer, 0), VK_SUCCESS);

    const VkCommandBufferBeginInfo command_buffer_begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) ==
          VK_SUCCESS);

    const VkRenderingAttachmentInfo color_attachment_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = vk.swapchain_image_views[image_index],
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

    UniformBufferObject ubo{};

    {
      static Vec3 trans = Vec3{0, 0, 0};

      if (pressed_keys.contains(up_keycode)) {
        trans.y += 1.f;
      } else if (pressed_keys.contains(down_keycode)) {
        trans.y -= 1.f;
      }

      if (pressed_keys.contains(right_keycode)) {
        trans.x += 1.f;
      } else if (pressed_keys.contains(left_keycode)) {
        trans.x -= 1.f;
      }

      ubo.model = Translate(trans);
      ubo.view = Identity4;

      ubo.proj =
          Ortho(vk.surface_extent.width / -2.f, vk.surface_extent.width / 2.f,
                vk.surface_extent.height / 2.f, vk.surface_extent.height / -2.f,
                0.f, 1.f);

      memcpy(uniform_buffers_mapped[image_index], &ubo, sizeof(ubo));
    }

    {
      const VkImageMemoryBarrier image_memory_barrier{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,  // TODO:
          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .image = vk.swapchain_images[image_index],
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

      const VkViewport viewport{
          .x = 0.0f,
          .y = 0.0f,
          .width = static_cast<float>(vk.surface_extent.width),
          .height = static_cast<float>(vk.surface_extent.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      };
      vkCmdSetViewport(command_buffer, 0, 1, &viewport);

      const VkRect2D scissor{
          .offset = {0, 0},
          .extent = vk.surface_extent,
      };
      vkCmdSetScissor(command_buffer, 0, 1, &scissor);

      vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_layout, 0, 1,
                              &descriptor_sets[image_index], 0, nullptr);
      vkCmdDraw(command_buffer, vertices.size(), 1, 0, 0);
    }
    vkCmdEndRendering(command_buffer);

    {
      const VkImageMemoryBarrier image_memory_barrier{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .image = vk.swapchain_images[image_index],
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

    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, frame_fence));

    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &submit_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &vk.swapchain,
        .pImageIndices = &image_index,
    };

    VkResult present_result = vkQueuePresentKHR(queue, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_ERROR_OUT_OF_DATE_KHR) {
      CreateSwapchain();
      continue;
    }
    VK_CHECK(present_result);
  }

  return 0;
}

}  // namespace nyla

int main() { return nyla::Main(); }
