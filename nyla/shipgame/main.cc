#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/commons/clock.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec2.h"
#include "nyla/commons/math/vec3.h"
#include "nyla/commons/readfile.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"

namespace nyla {

struct Vertex {
  struct {
    float x;
    float y;
  } pos;
  Vec3 color;
};

static bool running = true;
static xcb_window_t window;
static absl::flat_hash_set<xcb_keycode_t> pressed_keys;

VkExtent2D Vulkan_PlatformGetWindowExtent() {
  xcb_get_geometry_reply_t* window_geometry = xcb_get_geometry_reply(
      x11.conn, xcb_get_geometry(x11.conn, window), nullptr);

  return {.width = window_geometry->width, .height = window_geometry->height};
}

void Vulkan_PlatformSetSurface() {
  const VkXcbSurfaceCreateInfoKHR surface_create_info{
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = x11.conn,
      .window = window,
  };
  VK_CHECK(vkCreateXcbSurfaceKHR(vk.instance, &surface_create_info, nullptr,
                                 &vk.surface));
}

static void ProcessXEvents() {
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

        if (clientmessage->format == 32 &&
            clientmessage->type == x11.atoms.wm_protocols &&
            clientmessage->data.data32[0] == x11.atoms.wm_delete_window) {
          running = false;
        }
        break;
      }
    }
  }
}

static int Main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  X11_Initialize();

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
  xcb_keycode_t acceleration_keycode;
  xcb_keycode_t brake_keycode;
  xcb_keycode_t fire_keycode;

  {
    X11_KeyResolver key_resolver;
    X11_InitializeKeyResolver(key_resolver);

    up_keycode = X11_ResolveKeyCode(key_resolver, "AD03");
    left_keycode = X11_ResolveKeyCode(key_resolver, "AC02");
    down_keycode = X11_ResolveKeyCode(key_resolver, "AC03");
    right_keycode = X11_ResolveKeyCode(key_resolver, "AC04");
    acceleration_keycode = X11_ResolveKeyCode(key_resolver, "SPCE");
    brake_keycode = X11_ResolveKeyCode(key_resolver, "AC07");
    fire_keycode = X11_ResolveKeyCode(key_resolver, "AC08");

    X11_FreeKeyResolver(key_resolver);
  }

  Vulkan_Initialize();

  //

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
    Vulkan_CreateBuffer(sizeof(UniformBufferObject),
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
          .module = Vulkan_CreateShaderModule(
              ReadFile("nyla/vulkan/shaders/vert.spv")),
          .pName = "main",
      },
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = Vulkan_CreateShaderModule(
              ReadFile("nyla/vulkan/shaders/frag.spv")),
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

  const std::vector<Vertex> vertices = {
      {{-25.f, -18.f}, {0.0f, 0.0f, 1.0f}},
      {{25.f, 0.f}, {0.0f, 1.0f, 0.0f}},
      {{-25.f, 18.f}, {1.0f, 0.0f, 0.0f}},
  };

  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;
  Vulkan_CreateBuffer(vk.command_pool, vk.queue,
                      sizeof(vertices[0]) * vertices.size(), vertices.data(),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer,
                      vertex_buffer_memory);

  VkPipeline graphics_pipeline = Vulkan_CreateGraphicsPipeline(
      vertex_input_create_info, pipeline_layout, shader_stages);

  struct Profiling {
    uint16_t xevent;
  };
  std::array<Profiling, 3> prof_data;
  uint8_t iprof = 0;

  for (uint8_t iframe = 0; iframe < kVulkan_NumFramesInFlight;
       iframe = ((iframe + 1) % kVulkan_NumFramesInFlight)) {
    Profiling& prof = prof_data[iprof];
    prof = {};
    iprof = (iprof + 1) % prof_data.size();

    //

    prof.xevent = GetMonotonicTimeMicros();
    ProcessXEvents();
    prof.xevent = GetMonotonicTimeMicros() - prof.xevent;

    //

    if (!running) break;

    static Vulkan_FrameData frame_data;
    Vulkan_FrameBegin(frame_data);

    if (frame_data.dt > .05f) {
      LOG(INFO) << "dts spike: " << frame_data.dt << "s";

      const Profiling& lastprof =
          prof_data[(iprof + prof_data.size() - 2) % prof_data.size()];
      LOG(INFO) << "last frame prof: xevent=" << lastprof.xevent;
    }

    {
      struct ShipState {
        Vec2 pos;
        Vec2 velocity;
        float dir_radians;
      };
      static ShipState ship_state;

      const int dx = (pressed_keys.contains(right_keycode) ? 1 : 0) -
                     (pressed_keys.contains(left_keycode) ? 1 : 0);
      const int dy = (pressed_keys.contains(down_keycode) ? 1 : 0) -
                     (pressed_keys.contains(up_keycode) ? 1 : 0);

      constexpr float step = 1e-5;
      for (float accumulator = 0; accumulator < frame_data.dt;
           accumulator += step) {
        if (dx || dy) {
          float angle =
              std::atan2(-static_cast<float>(dy), static_cast<float>(dx));
          if (angle < 0.f) {
            angle += 2.f * pi;
          }

          ship_state.dir_radians =
              LerpAngle(ship_state.dir_radians, angle, step * 20.f);
        }

        if (pressed_keys.contains(acceleration_keycode)) {
          const Vec2 direction = Normalize(Vec2{
              .x = std::cos(ship_state.dir_radians),
              .y = std::sin(ship_state.dir_radians),
          });
          Lerp(ship_state.velocity, direction * 2000.f, step);
        } else {
          Lerp(ship_state.velocity, Vec2{},
               pressed_keys.contains(brake_keycode) ? 2.f * step : step);
        }

        ship_state.pos += ship_state.velocity * step;
      }

      const UniformBufferObject ubo{
          .model =
              Mult(Translate(Vec3{ship_state.pos.x, ship_state.pos.y, 0.f}),
                   Rotate2D(ship_state.dir_radians)),
          .view = Identity4,
          .proj = Ortho(vk.surface_extent.width / -2.f,
                        vk.surface_extent.width / 2.f,
                        vk.surface_extent.height / 2.f,
                        vk.surface_extent.height / -2.f, 0.f, 1.f),
      };
      memcpy(uniform_buffers_mapped[frame_data.swapchain_image_index], &ubo,
             sizeof(ubo));
    }

    Vulkan_RenderingBegin(graphics_pipeline, frame_data);
    {
      const VkCommandBuffer command_buffer =
          vk.command_buffers[frame_data.iframe];

      VkBuffer vertex_buffers[] = {vertex_buffer};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

      vkCmdBindDescriptorSets(
          command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0,
          1, &descriptor_sets[frame_data.swapchain_image_index], 0, nullptr);
      vkCmdDraw(command_buffer, vertices.size(), 1, 0, 0);
    }
    Vulkan_RenderingEnd(graphics_pipeline, frame_data);

    Vulkan_FrameEnd(frame_data);
  }
  return 0;
}

}  // namespace nyla

int main() { return nyla::Main(); };
