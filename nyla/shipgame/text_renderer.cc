#include "nyla/shipgame/text_renderer.h"

#include <array>
#include <cstdint>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/readfile.h"
#include "nyla/vulkan/vulkan.h"

namespace {

struct TextRendererLineUBO {
  uint32_t words[68];
  int origin_x;
  int origin_y;
  uint32_t word_count;
  int pad;
  float fg[4];
  float bg[4];
};

struct PerFrame {
  PerFrame(size_t n) : descriptor_sets(n), text_line_ubo(n) {}

  std::vector<VkDescriptorSet> descriptor_sets;

  struct Uniform {
    Uniform(size_t n) : buffer(n), memory(n), mapped(n) {}

    std::vector<VkBuffer> buffer;
    std::vector<VkDeviceMemory> memory;
    std::vector<void*> mapped;
  };

  Uniform text_line_ubo;
};

}  // namespace

namespace nyla {

static TextRendererLineUBO submit_buffer[16];
static uint8_t isubmit;

static VkPipelineLayout pipeline_layout;
static VkPipeline pipeline;
static PerFrame* per_frame;

void InitTextRenderer() {
  per_frame = new PerFrame(kVulkan_NumFramesInFlight);

  const auto shader_stages = std::to_array<VkPipelineShaderStageCreateInfo>({
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,
          .module = Vulkan_CreateShaderModule(
              ReadFile("nyla/shipgame/shaders/psf2_ansii_vert.spv")),
          .pName = "main",
      },
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = Vulkan_CreateShaderModule(
              ReadFile("nyla/shipgame/shaders/psf2_ansii_frag.spv")),
          .pName = "main",
      },
  });

  const VkDescriptorPool descriptor_pool = [] {
    const VkDescriptorPoolSize descriptor_pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
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
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
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
        per_frame->descriptor_sets[i], 0, true,
        sizeof(TextRendererLineUBO) * std::size(submit_buffer),
        sizeof(TextRendererLineUBO), per_frame->text_line_ubo.buffer[i],
        per_frame->text_line_ubo.memory[i], per_frame->text_line_ubo.mapped[i]);
  }

  const VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .pVertexBindingDescriptions = nullptr,
      .vertexAttributeDescriptionCount = 0,
      .pVertexAttributeDescriptions = nullptr,
  };

  pipeline = Vulkan_CreateGraphicsPipeline(vertex_input_create_info,
                                           pipeline_layout, shader_stages);
}

void RenderText(int32_t x, int32_t y, std::string_view text) {
  CHECK_LT(isubmit, std::size(submit_buffer));

  TextRendererLineUBO& ubo = submit_buffer[isubmit++];

  ubo.origin_x = x;
  ubo.origin_y = y;

  ubo.fg[0] = 1.f;
  ubo.fg[1] = 1.f;
  ubo.fg[2] = 1.f;
  ubo.fg[3] = 1.f;

  ubo.word_count = std::min<size_t>((text.size() + 3) / 4, 68);
  size_t nbytes = std::min(text.size(), size_t(ubo.word_count) * 4);

  for (size_t i = 0; i < ubo.word_count; ++i) {
    uint32_t w = 0;
    for (uint8_t j = 0; j < 4; ++j) {
      size_t idx = i * 4 + j;
      if (idx < nbytes) {
        w |= (uint32_t(uint8_t(text[idx])) << (8 * j));
      }
    }
    ubo.words[i] = w;
  }
}

void TextRendererBefore() {
  memcpy(per_frame->text_line_ubo.mapped[vk.current_frame_data.iframe],
         &submit_buffer, sizeof(TextRendererLineUBO) * isubmit);
}

void TextRendererRecord() {
  const VkCommandBuffer command_buffer =
      vk.command_buffers[vk.current_frame_data.iframe];

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  for (uint8_t i = 0; i < isubmit; ++i) {
    uint32_t offset = sizeof(TextRendererLineUBO) * i;

    vkCmdBindDescriptorSets(
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
        &per_frame->descriptor_sets[vk.current_frame_data.iframe], 1, &offset);

    vkCmdDraw(command_buffer, 3, 1, 0, 0);
  }
  isubmit = 0;
}

}  // namespace nyla
