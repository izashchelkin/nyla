#include <cstddef>
#include <cstdlib>
#include <random>
#include <vector>

#include "absl/cleanup/cleanup.h"
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
#include "nyla/commons/types.h"
#include "nyla/shipgame/circle.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"

namespace nyla {

struct Vertex {
  Vec2 pos;
  Vec3 color;
};

struct UniformBufferObject {
  Mat4 model;
  Mat4 view;
  Mat4 proj;
};

struct Asteroid {
  Vec2 pos;
};

struct Ship {
  Vec2 pos;
  Vec2 velocity;
  float dir_radians;
};

struct PerSwapchainImageData {
  PerSwapchainImageData(size_t num_swapchain_images)
      : uniform_buffers(num_swapchain_images),
        uniform_buffers_memory(num_swapchain_images),
        uniform_buffers_mapped(num_swapchain_images),
        descriptor_sets(num_swapchain_images) {}

  std::vector<VkBuffer> uniform_buffers;
  std::vector<VkDeviceMemory> uniform_buffers_memory;
  std::vector<void*> uniform_buffers_mapped;
  std::vector<VkDescriptorSet> descriptor_sets;
};

static bool running = true;
static xcb_window_t window;
static Set<xcb_keycode_t> pressed_keys;

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

  Vulkan_Initialize();

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

  //

  const VkDescriptorPoolSize descriptor_pool_size{
      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
      .descriptorCount = static_cast<uint32_t>(vk.swapchain_image_count()),
  };

  const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = static_cast<uint32_t>(vk.swapchain_image_count()),
      .poolSizeCount = 1,
      .pPoolSizes = &descriptor_pool_size,
  };

  VkDescriptorPool descriptor_pool;
  VK_CHECK(vkCreateDescriptorPool(vk.device, &descriptor_pool_create_info,
                                  nullptr, &descriptor_pool));

  const VkDescriptorSetLayoutBinding ubo_layout_binding{
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
  };

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &ubo_layout_binding,
  };

  VkDescriptorSetLayout descriptor_set_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(vk.device,
                                       &descriptor_set_layout_create_info,
                                       nullptr, &descriptor_set_layout));

  const std::vector<VkDescriptorSetLayout> layouts(vk.swapchain_image_count(),
                                                   descriptor_set_layout);

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  VkPipelineLayout pipeline_layout;
  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr,
                         &pipeline_layout);

  PerSwapchainImageData per_swapchain_image_data(vk.swapchain_image_count());

  const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
  };

  VK_CHECK(vkAllocateDescriptorSets(
      vk.device, &descriptor_set_alloc_info,
      per_swapchain_image_data.descriptor_sets.data()));

  for (size_t i = 0; i < vk.swapchain_image_count(); ++i) {
    auto& uniform_buffer = per_swapchain_image_data.uniform_buffers[i];
    auto& uniform_buffer_memory =
        per_swapchain_image_data.uniform_buffers_memory[i];
    auto& uniform_buffer_mapped =
        per_swapchain_image_data.uniform_buffers_mapped[i];
    auto& descriptor_set = per_swapchain_image_data.descriptor_sets[i];

    size_t buffer_size = 11 * sizeof(UniformBufferObject);

    Vulkan_CreateBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        uniform_buffer, uniform_buffer_memory);

    vkMapMemory(vk.device, uniform_buffer_memory, 0, buffer_size, 0,
                &uniform_buffer_mapped);

    const VkDescriptorBufferInfo buffer_info{
        .buffer = uniform_buffer,
        .offset = 0,
        .range = sizeof(UniformBufferObject),
    };

    const VkWriteDescriptorSet descriptor_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_info,
        .pTexelBufferView = nullptr,
    };

    vkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, nullptr);
  }

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

  VkPipeline graphics_pipeline = Vulkan_CreateGraphicsPipeline(
      vertex_input_create_info, pipeline_layout, shader_stages);

  VkBuffer ship_vertex_buffer;
  VkDeviceMemory ship_vertex_buffer_memory;
  const std::vector<Vertex> ship_vertices = {
      {{-25.f, -18.f}, {0.0f, 0.0f, 1.0f}},
      {{25.f, 0.f}, {0.0f, 1.0f, 0.0f}},
      {{-25.f, 18.f}, {1.0f, 0.0f, 0.0f}},
  };
  {
    Vulkan_CreateBuffer(vk.command_pool, vk.queue,
                        sizeof(ship_vertices[0]) * ship_vertices.size(),
                        ship_vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        ship_vertex_buffer, ship_vertex_buffer_memory);
  }

  VkBuffer asteroid_vertex_buffer;
  VkDeviceMemory asteroid_vertex_buffer_memory;
  std::vector<Vertex> asteroid_vertices;
  {
    for (size_t i = 0; i < 10; ++i) {
      std::vector<Vec2> asteroid_vertex_positions = TriangulateCircle(6, 25);
      for (const auto& pos : asteroid_vertex_positions) {
        asteroid_vertices.emplace_back(Vertex{pos, Vec3{1.f, 0.f, 1.f}});
      }
    }

    Vulkan_CreateBuffer(vk.command_pool, vk.queue,
                        sizeof(asteroid_vertices[0]) * asteroid_vertices.size(),
                        asteroid_vertices.data(),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        asteroid_vertex_buffer, asteroid_vertex_buffer_memory);
  }

  struct Profiling {
    uint16_t xevent;
  };
  std::array<Profiling, 3> prof_data;
  uint8_t iprof = 0;

  for (;;) {
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

    static Ship ship;
    static std::array<Asteroid, 10> asteroids = [] {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<float> dist(-300.f, 300.f);

      std::array<Asteroid, 10> a{};
      for (auto& s : a) {
        s.pos.x = dist(gen);
        s.pos.y = dist(gen);
      }
      return a;
    }();

    static Vec2 camera_pos = {};

    {
      const int dx = (pressed_keys.contains(right_keycode) ? 1 : 0) -
                     (pressed_keys.contains(left_keycode) ? 1 : 0);
      const int dy = (pressed_keys.contains(down_keycode) ? 1 : 0) -
                     (pressed_keys.contains(up_keycode) ? 1 : 0);

      constexpr float step = 1e-5;
      for (float accumulator = 0; accumulator < frame_data.dt;
           accumulator += step) {
        {
          if (dx || dy) {
            float angle =
                std::atan2(-static_cast<float>(dy), static_cast<float>(dx));
            if (angle < 0.f) {
              angle += 2.f * pi;
            }

            ship.dir_radians = LerpAngle(ship.dir_radians, angle, step * 2.5f);
          }

          if (pressed_keys.contains(acceleration_keycode)) {
            const Vec2 direction = Normalize(Vec2{
                std::cos(ship.dir_radians),
                std::sin(ship.dir_radians),
            });
            Lerp(ship.velocity, direction * 2000.f, step);
          } else {
            Lerp(ship.velocity, Vec2{},
                 pressed_keys.contains(brake_keycode) ? 2.f * step : step);
          }

          ship.pos += ship.velocity * step;
        }

        {
          Lerp(camera_pos, ship.pos, 5.f * step);
        }
      }

      const Mat4 proj =
          Ortho(vk.surface_extent.width / -2.f, vk.surface_extent.width / 2.f,
                vk.surface_extent.height / 2.f, vk.surface_extent.height / -2.f,
                0.f, 1.f);

      const Mat4 view = Translate(Vec3{
          -camera_pos.x,
          -camera_pos.y,
          0.f,
      });

      UniformBufferObject ubos[11];
      ubos[0] = {
          .model = Mult(Translate(Vec3{ship.pos.x, ship.pos.y, 0.f}),
                        Rotate2D(ship.dir_radians)),
          .view = view,
          .proj = proj,
      };

      for (int i = 0; i < 10; ++i) {
        const auto& asteroid = asteroids[i];
        ubos[i + 1] = {
            .model = Translate(Vec3{asteroid.pos.x, asteroid.pos.y, 0.f}),
            .view = view,
            .proj = proj,
        };
      }

      memcpy(per_swapchain_image_data
                 .uniform_buffers_mapped[frame_data.swapchain_image_index],
             &ubos, sizeof(ubos));
    }

    Vulkan_RenderingBegin(graphics_pipeline, frame_data);
    {
      const VkCommandBuffer command_buffer =
          vk.command_buffers[frame_data.iframe];

      {
        uint32_t offset0 = 0;
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0,
            1,
            &per_swapchain_image_data
                 .descriptor_sets[frame_data.swapchain_image_index],
            1, &offset0);

        VkBuffer vertex_buffers[] = {ship_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
        vkCmdDraw(command_buffer, ship_vertices.size(), 1, 0, 0);
      }

      for (int i = 1; i < 11; ++i) {
        uint32_t offset1 = i * sizeof(UniformBufferObject);
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0,
            1,
            &per_swapchain_image_data
                 .descriptor_sets[frame_data.swapchain_image_index],
            1, &offset1);

        VkBuffer vertex_buffers[] = {asteroid_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
        vkCmdDraw(command_buffer, asteroid_vertices.size(), 1, 0, 0);
      }
    }
    Vulkan_RenderingEnd(graphics_pipeline, frame_data);
    Vulkan_FrameEnd(frame_data);
  }
  return 0;
}

}  // namespace nyla

int main() { return nyla::Main(); };
