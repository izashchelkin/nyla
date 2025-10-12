#include "nyla/vulkan/vulkan.h"

#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#define VK_USE_PLATFORM_XCB_KHR

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>

#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "vulkan/vulkan.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {}  // namespace nyla

static std::vector<char> ReadFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  CHECK(file.is_open());

  std::vector<char> buffer(file.tellg());

  file.seekg(0);
  file.read(buffer.data(), buffer.size());

  file.close();
  return buffer;
}

static VkShaderModule CreateShaderModule(VkDevice device,
                                         const std::vector<char>& code) {
  VkShaderModuleCreateInfo shader_module_info{};
  shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_info.codeSize = code.size();
  shader_module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shader_module;
  CHECK(vkCreateShaderModule(device, &shader_module_info, nullptr,
                             &shader_module) == VK_SUCCESS);
  return shader_module;
}

static void RecordCommandBuffer(VkCommandBuffer command_buffer,
                                VkRenderPass render_pass,
                                VkFramebuffer framebuffer, VkExtent2D extent,
                                VkPipeline graphics_pipeline) {
  VkCommandBufferBeginInfo command_buffer_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) ==
        VK_SUCCESS);

  VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  VkRenderPassBeginInfo render_pass_begin_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = render_pass,
      .framebuffer = framebuffer,
      .renderArea =
          {
              .offset = {0, 0},
              .extent = extent,
          },
      .clearValueCount = 1,
      .pClearValues = &clear_color,
  };

  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info,
                       VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    graphics_pipeline);

  VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);

  VkRect2D scissor{
      .offset = {0, 0},
      .extent = extent,
  };
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);

  vkCmdDraw(command_buffer, 3, 1, 0, 0);

  vkCmdEndRenderPass(command_buffer);

  CHECK_EQ(vkEndCommandBuffer(command_buffer), VK_SUCCESS);
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
    auto instance_extensions = std::to_array({
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
    });

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.enabledExtensionCount = instance_extensions.size();
    create_info.ppEnabledExtensionNames = instance_extensions.data();

    VkInstance instance;
    CHECK_EQ(vkCreateInstance(&create_info, nullptr, &instance), VK_SUCCESS);

    return instance;
  }();

  VkPhysicalDevice phys_device = [=]() {
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

  VkDevice device = [=]() {
    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    auto device_extensions = std::to_array({
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    });

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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

  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages;

  auto vert_shader_module =
      CreateShaderModule(device, ReadFile("nyla/vulkan/shaders/vert.spv"));
  shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shader_stages[0].module = vert_shader_module;
  shader_stages[0].pName = "main";

  auto frag_shader_module =
      CreateShaderModule(device, ReadFile("nyla/vulkan/shaders/frag.spv"));
  shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shader_stages[1].module = frag_shader_module;
  shader_stages[1].pName = "main";

  auto dynamic_states = std::to_array<VkDynamicState>({
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  });

  VkPipelineDynamicStateCreateInfo dynamic_state_create_info{};
  dynamic_state_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_create_info.dynamicStateCount = dynamic_states.size();
  dynamic_state_create_info.pDynamicStates = dynamic_states.data();

  VkPipelineVertexInputStateCreateInfo vertex_input_create_info{};
  vertex_input_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info{};
  input_assembly_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state_create_info;
  viewport_state_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state_create_info.viewportCount = 1;
  viewport_state_create_info.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer_create_info{};
  rasterizer_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer_create_info.depthClampEnable = VK_FALSE;
  rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
  rasterizer_create_info.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer_create_info.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling_create_info{};
  multisampling_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling_create_info.sampleShadingEnable = VK_FALSE;
  multisampling_create_info.minSampleShading = 1.0f;
  multisampling_create_info.alphaToCoverageEnable = VK_FALSE;
  multisampling_create_info.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.blendEnable = VK_FALSE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blending_create_info{};
  color_blending_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending_create_info.logicOpEnable = VK_FALSE;
  color_blending_create_info.logicOp = VK_LOGIC_OP_COPY;
  color_blending_create_info.attachmentCount = 1;
  color_blending_create_info.pAttachments = &color_blend_attachment;
  // color_blending_create_info.blendConstants = ;

  VkPipelineLayout pipeline_layout = [=]() {
    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout pipeline_layout;
    vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                           &pipeline_layout);
    return pipeline_layout;
  }();

  VkAttachmentDescription color_attachment{};
  color_attachment.format = surface_format.format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref{};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_create_info{};
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.attachmentCount = 1;
  render_pass_create_info.pAttachments = &color_attachment;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass;
  render_pass_create_info.dependencyCount = 1;
  render_pass_create_info.pDependencies = &dependency;

  VkRenderPass render_pass;
  CHECK_EQ(vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                              &render_pass),
           VK_SUCCESS);

  VkPipeline graphics_pipeline = [=]() {
    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    pipeline_create_info.stageCount = 2;
    pipeline_create_info.pStages = shader_stages.data();
    pipeline_create_info.pVertexInputState = &vertex_input_create_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly_create_info;
    pipeline_create_info.pViewportState = &viewport_state_create_info;
    pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    pipeline_create_info.pMultisampleState = &multisampling_create_info;
    pipeline_create_info.pDepthStencilState = nullptr;
    pipeline_create_info.pColorBlendState = &color_blending_create_info;
    pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    pipeline_create_info.layout = pipeline_layout;
    pipeline_create_info.renderPass = render_pass;
    pipeline_create_info.subpass = 0;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = -1;

    VkPipeline graphics_pipeline;
    CHECK_EQ(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                       &pipeline_create_info, nullptr,
                                       &graphics_pipeline),
             VK_SUCCESS);
    return graphics_pipeline;
  }();

  std::vector<VkFramebuffer> swapchain_fbs(swapchain_image_views.size());
  for (size_t i = 0; i < swapchain_image_views.size(); ++i) {
    VkFramebufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create_info.renderPass = render_pass;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &swapchain_image_views[i];
    create_info.width = surface_extent.width;
    create_info.height = surface_extent.height;
    create_info.layers = 1;

    CHECK_EQ(
        vkCreateFramebuffer(device, &create_info, nullptr, &swapchain_fbs[i]),
        VK_SUCCESS);
  }

  VkCommandPoolCreateInfo command_pool_create_info{};
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.flags =
      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool_create_info.queueFamilyIndex = queue_family_index;

  VkCommandPool command_pool;
  CHECK(vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                            &command_pool) == VK_SUCCESS);

  VkCommandBuffer command_buffer = [=]() {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    CHECK_EQ(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer),
             VK_SUCCESS);
    return command_buffer;
  }();

  VkSemaphoreCreateInfo semaphore_create_info{};
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkSemaphore image_available_semaphore;
  CHECK_EQ(vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                             &image_available_semaphore),
           VK_SUCCESS);

  VkSemaphore render_finished_semaphore;
  CHECK_EQ(vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                             &render_finished_semaphore),
           VK_SUCCESS);

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VkFence in_flight_fence;
  CHECK_EQ(vkCreateFence(device, &fence_info, nullptr, &in_flight_fence),
           VK_SUCCESS);

  for (;;) {
    CHECK_EQ(vkWaitForFences(device, 1, &in_flight_fence, VK_TRUE,
                             std::numeric_limits<uint64_t>::max()),
             VK_SUCCESS);

    vkResetFences(device, 1, &in_flight_fence);

    uint32_t image_index;

    VkResult acquire_result = vkAcquireNextImageKHR(
        device, swapchain, std::numeric_limits<uint64_t>::max(),
        image_available_semaphore, VK_NULL_HANDLE, &image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) break;
    CHECK((acquire_result == VK_SUBOPTIMAL_KHR) ||
          (acquire_result == VK_SUCCESS));

    CHECK_EQ(vkResetCommandBuffer(command_buffer, 0), VK_SUCCESS);

    RecordCommandBuffer(command_buffer, render_pass,
                        swapchain_fbs.at(image_index), surface_extent,
                        graphics_pipeline);

    VkSemaphore wait_semaphores[] = {image_available_semaphore};
    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[] = {render_finished_semaphore};

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    CHECK_EQ(vkQueueSubmit(queue, 1, &submit_info, in_flight_fence),
             VK_SUCCESS);

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;

    vkQueuePresentKHR(queue, &present_info);
  }
}
