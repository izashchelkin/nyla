#include "nyla/shipgame/entity_renderer.h"

#include <unistd.h>

#include <array>

#include "nyla/commons/readfile.h"
#include "nyla/shipgame/gamecommon.h"
#include "nyla/vulkan/vulkan.h"

namespace {

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

}  // namespace

namespace nyla {

static VkPipeline pipeline;
static VkPipelineLayout pipeline_layout;

static PerSwapchainImageData* per_swapchain_image_data;

void InitEntityRenderer() {
  per_swapchain_image_data =
      new PerSwapchainImageData(vk.swapchain_image_count());

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
        per_swapchain_image_data->descriptor_sets.data()));
  }

  for (size_t i = 0; i < vk.swapchain_image_count(); ++i) {
    CreateUniformBuffer(per_swapchain_image_data->descriptor_sets[i], 0, false,
                        sizeof(SceneUbo), sizeof(SceneUbo),
                        per_swapchain_image_data->scene_ubo.buffer[i],
                        per_swapchain_image_data->scene_ubo.memory[i],
                        per_swapchain_image_data->scene_ubo.mapped[i]);

    CreateUniformBuffer(per_swapchain_image_data->descriptor_sets[i], 1, true,
                        std::size(entities) * sizeof(EntityUbo),
                        sizeof(EntityUbo),
                        per_swapchain_image_data->entity_ubo.buffer[i],
                        per_swapchain_image_data->entity_ubo.memory[i],
                        per_swapchain_image_data->entity_ubo.mapped[i]);
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

void EntityRendererBefore(Vec2f camera_pos, float zoom) {
  const SceneUbo scene_ubo = {
      .view = Translate(Vec2fNeg(camera_pos)),
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

  memcpy(per_swapchain_image_data->scene_ubo
             .mapped[vk.current_frame_data.swapchain_image_index],
         &scene_ubo, sizeof(scene_ubo));
  memcpy(per_swapchain_image_data->entity_ubo
             .mapped[vk.current_frame_data.swapchain_image_index],
         &entity_ubos, sizeof(entity_ubos));
}

void EntityRendererRecord() {
  const VkCommandBuffer command_buffer =
      vk.command_buffers[vk.current_frame_data.iframe];

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  VkBuffer last_vertex_buffer = nullptr;
  uint32_t last_vertex_offset = 0;

  for (size_t i = 0; i < std::size(entities); ++i) {
    const auto& entity = entities[i];

    if (!entity.exists) continue;

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
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
        &per_swapchain_image_data
             ->descriptor_sets[vk.current_frame_data.swapchain_image_index],
        1, &ubo_offset);

    vkCmdDraw(command_buffer, entity.vertex_count, 1, 0, 0);
  }
}

}  // namespace nyla
