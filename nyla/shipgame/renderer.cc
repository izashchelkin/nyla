#include "nyla/shipgame/renderer.h"

#include <unistd.h>

#include <array>
#include <cstdint>

#include "absl/log/log.h"
#include "nyla/commons/math/mat4.h"
#include "nyla/commons/readfile.h"
#include "nyla/shipgame/game.h"
#include "nyla/vulkan/vulkan.h"

namespace {

struct PerFrame {
  PerFrame(size_t n) : descriptor_sets(n), scene_ubo(n), gameobject_ubo(n) {}

  std::vector<VkDescriptorSet> descriptor_sets;

  struct Uniform {
    Uniform(size_t n) : buffer(n), memory(n), mapped(n) {}

    std::vector<VkBuffer> buffer;
    std::vector<VkDeviceMemory> memory;
    std::vector<void*> mapped;
  };

  Uniform scene_ubo;
  Uniform gameobject_ubo;
};

}  // namespace

namespace nyla {

static VkPipeline pipeline;
static VkPipelineLayout pipeline_layout;

static PerFrame* per_frame;

void RenderGameObjectInitialize() {
  per_frame = new PerFrame(kVulkan_NumFramesInFlight);

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

  const VkDescriptorPool descriptor_pool = [] {
    const VkDescriptorPoolSize descriptor_pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = static_cast<uint32_t>(kVulkan_NumFramesInFlight),
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = static_cast<uint32_t>(kVulkan_NumFramesInFlight),
        },
    };

    const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = static_cast<uint32_t>(kVulkan_NumFramesInFlight),
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

  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr,
                         &pipeline_layout);

  {
    const std::vector<VkDescriptorSetLayout> descriptor_sets_layouts(
        kVulkan_NumFramesInFlight, descriptor_set_layout);

    const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount =
            static_cast<uint32_t>(descriptor_sets_layouts.size()),
        .pSetLayouts = descriptor_sets_layouts.data(),
    };

    VK_CHECK(vkAllocateDescriptorSets(vk.device, &descriptor_set_alloc_info,
                                      per_frame->descriptor_sets.data()));
  }

  for (size_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
    CreateUniformBuffer(
        per_frame->descriptor_sets[i], 0, false, sizeof(SceneUbo),
        sizeof(SceneUbo), per_frame->scene_ubo.buffer[i],
        per_frame->scene_ubo.memory[i], per_frame->scene_ubo.mapped[i]);

    CreateUniformBuffer(per_frame->descriptor_sets[i], 1, true,
                        (256 * sizeof(GameObjectUbo)), sizeof(GameObjectUbo),
                        per_frame->gameobject_ubo.buffer[i],
                        per_frame->gameobject_ubo.memory[i],
                        per_frame->gameobject_ubo.mapped[i]);
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

  pipeline = Vulkan_CreateGraphicsPipeline(vertex_input_create_info,
                                           pipeline_layout, shader_stages);
}

constexpr float game_units_on_screen_y = 2000.f;

void RenderGameObjectPrepareScene(Vec2f camera_pos, float zoom) {
  const float aspect = static_cast<float>(vk.surface_extent.width) /
                       static_cast<float>(vk.surface_extent.height);

  const float world_h = game_units_on_screen_y * zoom;
  const float world_w = world_h * aspect;

  const SceneUbo scene_ubo = {
      .view = Translate(Vec2fNeg(camera_pos)),
      .proj = Ortho(-world_w * .5f, world_w * .5f, world_h * .5f,
                    -world_h * .5f, 0.f, 1.f),
  };

  memcpy(per_frame->scene_ubo.mapped[vk.current_frame_data.iframe], &scene_ubo,
         sizeof(scene_ubo));
}

static size_t irender = 0;
static struct {
  GameObject obj[256];
  GameObjectUbo ubo[256];
} renderq;

void RenderGameObject(const GameObject& obj) {
  renderq.obj[irender] = obj;
  auto& ubo = renderq.ubo[irender];
  ++irender;

  ubo = {};
  ubo.model = Translate(obj.pos);
  ubo.model = Mult(ubo.model, Rotate2D(obj.angle_radians));
  ubo.model = Mult(ubo.model, Scale2D(200.f));
}

void RenderGameObjectFlushUniforms() {
  memcpy(per_frame->gameobject_ubo.mapped[vk.current_frame_data.iframe],
         &renderq.ubo, sizeof(renderq.ubo[0]) * std::size(renderq.ubo));
}

void RenderGameObjectRecord() {
  CHECK_LT(irender, (size_t)256);

  const VkCommandBuffer command_buffer =
      vk.command_buffers[vk.current_frame_data.iframe];

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  for (size_t i = 0; i < irender; ++i) {
    const auto& obj = renderq.obj[i];

    VkBuffer vertex_buffer = obj.vertex_buffer[vk.current_frame_data.iframe];
    CHECK(vertex_buffer);

    const VkDeviceSize offset = 0 * sizeof(Vertex);
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &offset);

    const uint32_t ubo_offset = i * sizeof(GameObjectUbo);
    vkCmdBindDescriptorSets(
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
        &per_frame->descriptor_sets[vk.current_frame_data.iframe], 1,
        &ubo_offset);

    vkCmdDraw(command_buffer, obj.vertex_count, 1, 0, 0);
  }

  irender = 0;
}

}  // namespace nyla
