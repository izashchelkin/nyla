#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <random>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "nyla/commons/clock.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec2.h"
#include "nyla/commons/math/vec3.h"
#include "nyla/commons/readfile.h"
#include "nyla/commons/types.h"
#include "nyla/shipgame/circle.h"
#include "nyla/shipgame/text_renderer.h"
#include "nyla/vulkan/vulkan.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"

namespace nyla {

struct Vertex {
  Vec2 pos;
  Vec3 color;
};

struct Entity {
  uint32_t flags;
  Vec2 pos;
  Vec2 velocity;
  float mass;
  float density;
  float angle_radians;

  Vec2 hit_rect;

  uint32_t vertex_count;
  uint32_t vertex_offset;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;
};
uint16_t ientity;
static Entity entities[256];

struct SceneUbo {
  Mat4 view;
  Mat4 proj;
};

struct EntityUbo {
  Mat4 model;
};

struct PerSwapchainImageData {
  PerSwapchainImageData(size_t num_swapchain_images)
      : descriptor_sets(num_swapchain_images),
        scene_ubo(num_swapchain_images),
        entity_ubo(num_swapchain_images) {}

  std::vector<VkDescriptorSet> descriptor_sets;

  struct Uniform {
    Uniform(size_t num_swapchain_images)
        : buffer(num_swapchain_images),
          memory(num_swapchain_images),
          mapped(num_swapchain_images) {}

    std::vector<VkBuffer> buffer;
    std::vector<VkDeviceMemory> memory;
    std::vector<void*> mapped;
  };

  Uniform scene_ubo;
  Uniform entity_ubo;
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
  InitLogging();

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

  PerSwapchainImageData per_swapchain_image_data(vk.swapchain_image_count());

  CHECK_EQ(vk.swapchain_image_count(),
           per_swapchain_image_data.entity_ubo.buffer.size());

  xcb_keycode_t up_keycode;
  xcb_keycode_t left_keycode;
  xcb_keycode_t down_keycode;
  xcb_keycode_t right_keycode;
  xcb_keycode_t acceleration_keycode;
  xcb_keycode_t boost_keycode;
  xcb_keycode_t brake_keycode;
  xcb_keycode_t fire_keycode;

  {
    X11_KeyResolver key_resolver;
    X11_InitializeKeyResolver(key_resolver);

    up_keycode = X11_ResolveKeyCode(key_resolver, "AD03");
    left_keycode = X11_ResolveKeyCode(key_resolver, "AC02");
    down_keycode = X11_ResolveKeyCode(key_resolver, "AC03");
    right_keycode = X11_ResolveKeyCode(key_resolver, "AC04");
    acceleration_keycode = X11_ResolveKeyCode(key_resolver, "AC07");
    boost_keycode = X11_ResolveKeyCode(key_resolver, "AC08");
    brake_keycode = X11_ResolveKeyCode(key_resolver, "SPCE");
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

  const VkDescriptorPool descriptor_pool = [] {
    const VkDescriptorPoolSize descriptor_pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount =
                static_cast<uint32_t>(vk.swapchain_image_count()),
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount =
                static_cast<uint32_t>(vk.swapchain_image_count()),
        },
    };

    const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = static_cast<uint32_t>(vk.swapchain_image_count()),
        .poolSizeCount = std::size(descriptor_pool_sizes),
        .pPoolSizes = descriptor_pool_sizes,
    };

    VkDescriptorPool descriptor_pool;
    VK_CHECK(vkCreateDescriptorPool(vk.device, &descriptor_pool_create_info,
                                    nullptr, &descriptor_pool));
    return descriptor_pool;
  }();

  const VkDescriptorSetLayout descriptor_set_layout = [] {
    const VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
    };

    const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = std::size(descriptor_set_layout_bindings),
        .pBindings = descriptor_set_layout_bindings,
    };

    VkDescriptorSetLayout descriptor_set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(vk.device,
                                         &descriptor_set_layout_create_info,
                                         nullptr, &descriptor_set_layout));
    return descriptor_set_layout;
  }();

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  VkPipelineLayout pipeline_layout;
  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr,
                         &pipeline_layout);

  {
    const std::vector<VkDescriptorSetLayout> descriptor_sets_layouts(
        vk.swapchain_image_count(), descriptor_set_layout);

    const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount =
            static_cast<uint32_t>(descriptor_sets_layouts.size()),
        .pSetLayouts = descriptor_sets_layouts.data(),
    };

    VK_CHECK(vkAllocateDescriptorSets(
        vk.device, &descriptor_set_alloc_info,
        per_swapchain_image_data.descriptor_sets.data()));
  }

  for (size_t i = 0; i < vk.swapchain_image_count(); ++i) {
    CreateUniformBuffer(per_swapchain_image_data.descriptor_sets[i], 0, false,
                        sizeof(SceneUbo), sizeof(SceneUbo),
                        per_swapchain_image_data.scene_ubo.buffer[i],
                        per_swapchain_image_data.scene_ubo.memory[i],
                        per_swapchain_image_data.scene_ubo.mapped[i]);

    CreateUniformBuffer(per_swapchain_image_data.descriptor_sets[i], 1, true,
                        std::size(entities) * sizeof(EntityUbo),
                        sizeof(EntityUbo),
                        per_swapchain_image_data.entity_ubo.buffer[i],
                        per_swapchain_image_data.entity_ubo.memory[i],
                        per_swapchain_image_data.entity_ubo.mapped[i]);
  }

  const VkPipeline graphics_pipeline = [&] {
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
    return graphics_pipeline;
  }();

  InitTextRenderer();

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

    Vulkan_FrameBegin();

    if (vk.current_frame_data.dt > .05f) {
      LOG(INFO) << "dts spike: " << vk.current_frame_data.dt << "s";

      const Profiling& lastprof =
          prof_data[(iprof + prof_data.size() - 2) % prof_data.size()];
      LOG(INFO) << "last frame prof: xevent=" << lastprof.xevent;
    }

    static Entity& ship = *[] {
      Entity& ship = entities[ientity++];

      VkBuffer ship_vertex_buffer;
      VkDeviceMemory ship_vertex_buffer_memory;
      const std::vector<Vertex> ship_vertices = {
          {{-25.f, -18.f}, {1.0f, 0.0f, 0.3f}},
          {{25.f, 0.f}, {1.0f, 0.0f, 0.0f}},
          {{-25.f, 18.f}, {1.0f, 0.3f, 0.0f}},
      };

      Vulkan_CreateBuffer(vk.command_pool, vk.queue,
                          sizeof(ship_vertices[0]) * ship_vertices.size(),
                          ship_vertices.data(),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ship_vertex_buffer,
                          ship_vertex_buffer_memory);

      ship.flags = 1;
      ship.mass = 5;
      ship.density = 1;
      ship.vertex_count = ship_vertices.size();
      ship.vertex_buffer = ship_vertex_buffer;
      ship.vertex_buffer_memory = ship_vertex_buffer_memory;

      return &ship;
    }();

    static std::span<Entity> asteroids = [] {
      //

      std::span<Entity> asteroids = {&entities[ientity], 64};
      ientity += asteroids.size();

      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<float> dist(-x11.screen->width_in_pixels,
                                                 x11.screen->width_in_pixels);

      for (size_t i = 0; i < std::size(asteroids); ++i) {
        auto& asteroid = asteroids[i];

        asteroid.density = 0.0001;

        switch (i % 2) {
          case 0:
            asteroid.mass = 300;
          case 1:
            asteroid.mass = 50;
        }
        if (i == 0) asteroid.mass = 300000;

        float volume = asteroid.mass / asteroid.density;
        float radius = std::cbrt(volume * 3.f / 4.f / pi);

        std::vector<Vertex> asteroid_vertices;
        {
          std::vector<Vec2> asteroid_vertex_positions =
              TriangulateCircle(10, radius);
          asteroid_vertices.reserve(asteroid_vertex_positions.size());
          for (Vec2& pos : asteroid_vertex_positions) {
            asteroid_vertices.emplace_back(Vertex{pos, {.3f, 1.f, .3f}});
          }
        }

        VkBuffer asteroid_vertex_buffer;
        VkDeviceMemory asteroid_vertex_buffer_memory;
        Vulkan_CreateBuffer(
            vk.command_pool, vk.queue,
            sizeof(Vertex) * asteroid_vertices.size(), asteroid_vertices.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, asteroid_vertex_buffer,
            asteroid_vertex_buffer_memory);

        asteroid.flags = 3;

        asteroid.pos.x = dist(gen);
        asteroid.pos.y = dist(gen);

        asteroid.vertex_count = asteroid_vertices.size();
        asteroid.vertex_buffer = asteroid_vertex_buffer;
        asteroid.vertex_buffer_memory = asteroid_vertex_buffer_memory;
      }

      return asteroids;
    }();

    static Vec2 camera_pos = {};

    {
      const int dx = (pressed_keys.contains(right_keycode) ? 1 : 0) -
                     (pressed_keys.contains(left_keycode) ? 1 : 0);
      const int dy = (pressed_keys.contains(down_keycode) ? 1 : 0) -
                     (pressed_keys.contains(up_keycode) ? 1 : 0);

      static float dt_accumulator = 0.f;
      dt_accumulator += vk.current_frame_data.dt;

      constexpr float step = 1.f / 120.f;
      for (; dt_accumulator >= vk.current_frame_data.dt;
           dt_accumulator -= step) {
        {
          if (dx || dy) {
            float angle =
                std::atan2(-static_cast<float>(dy), static_cast<float>(dx));
            if (angle < 0.f) {
              angle += 2.f * pi;
            }

            ship.angle_radians =
                LerpAngle(ship.angle_radians, angle, step * 5.f);
          }

          if (pressed_keys.contains(brake_keycode)) {
            Lerp(ship.velocity, Vec2{}, step * 5.f);
          } else if (pressed_keys.contains(boost_keycode)) {
            const Vec2 direction = Normalize(Vec2{
                std::cos(ship.angle_radians),
                std::sin(ship.angle_radians),
            });
            Lerp(ship.velocity, direction * 5000.f, step);
          } else if (pressed_keys.contains(acceleration_keycode)) {
            const Vec2 direction = Normalize(Vec2{
                std::cos(ship.angle_radians),
                std::sin(ship.angle_radians),
            });
            Lerp(ship.velocity, direction * 1000.f, step * 8.f);
          } else {
            Lerp(ship.velocity, Vec2{}, step);
          }

          ship.pos += ship.velocity * step;
        }

        {
          Lerp(camera_pos, ship.pos, 5.f * step);
        }

        {
          for (size_t i = 0; i < std::size(entities); ++i) {
            auto& entity = entities[i];
            if ((entity.flags & 2) == 0) continue;

            Vec2 force_sum{};

            for (size_t j = 0; j < std::size(entities); ++j) {
              if (j == i) continue;
              if (!entities[j].mass) continue;

              Vec2 v = entities[j].pos - entity.pos;
              float r = Len(v);
              float F = 100 * 6.7f * entity.mass * entities[j].mass / (r * r);

              force_sum += (v / Len(v) * F);
            }

            entity.velocity += (force_sum / entity.mass) * step;
            entity.pos += entity.velocity * step;
          }
        }
      }

      float zoom = 1.f;

      const SceneUbo scene_ubo = {
          .view = Translate(Vec3{-camera_pos.x, -camera_pos.y, 0.f}),
          .proj = Ortho(vk.surface_extent.width * zoom / -2.f,
                        vk.surface_extent.width * zoom / 2.f,
                        vk.surface_extent.height * zoom / 2.f,
                        vk.surface_extent.height * zoom / -2.f, 0.f, 1.f),
      };

      EntityUbo entity_ubos[std::size(entities)];
      for (size_t i = 0; i < std::size(entities); ++i) {
        auto& ubo = entity_ubos[i];
        const auto& entity = entities[i];

        ubo.model = Mult(Translate(entity.pos), Rotate2D(entity.angle_radians));
      }

      memcpy(per_swapchain_image_data.scene_ubo
                 .mapped[vk.current_frame_data.swapchain_image_index],
             &scene_ubo, sizeof(scene_ubo));
      memcpy(per_swapchain_image_data.entity_ubo
                 .mapped[vk.current_frame_data.swapchain_image_index],
             &entity_ubos, sizeof(entity_ubos));
    }

    RenderText(200, 200, "Hello world!");
    RenderText(250, 250, "Hello world, here too!");

    TextRendererBefore();

    Vulkan_RenderingBegin();
    {
      const VkCommandBuffer command_buffer =
          vk.command_buffers[vk.current_frame_data.iframe];

#if 1
      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        graphics_pipeline);
      {
        VkBuffer last_vertex_buffer = nullptr;
        uint32_t last_vertex_offset = 0;

        for (size_t i = 0; i < std::size(entities); ++i) {
          const auto& entity = entities[i];

          if (!entity.flags) continue;

          if (last_vertex_buffer != entity.vertex_buffer ||
              last_vertex_offset != entity.vertex_offset) {
            last_vertex_buffer = entity.vertex_buffer;
            last_vertex_offset = entity.vertex_offset;

            const VkDeviceSize offset = last_vertex_offset * sizeof(Vertex);
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &last_vertex_buffer,
                                   &offset);
          }

          const uint32_t ubo_offset = i * sizeof(EntityUbo);
          vkCmdBindDescriptorSets(
              command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
              0, 1,
              &per_swapchain_image_data.descriptor_sets
                   [vk.current_frame_data.swapchain_image_index],
              1, &ubo_offset);

          vkCmdDraw(command_buffer, entity.vertex_count, 1, 0, 0);
        }
      }
#endif

      TextRendererRecord();
    }

    Vulkan_RenderingEnd();
    Vulkan_FrameEnd();
  }
  return 0;
}

}  // namespace nyla

int main() { return nyla::Main(); };
