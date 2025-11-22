#include "nyla/vulkan/render_pipeline.h"

#include <unistd.h>

#include <cstdint>

#include "absl/log/log.h"
#include "nyla/commons/memory/align.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

static void CreateMappedBuffer(VkBufferUsageFlags usage, size_t buffer_size, VkBuffer& buffer, VkDeviceMemory& memory,
                               void*& mapped) {
  Vulkan_CreateBuffer(buffer_size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buffer, memory);

  vkMapMemory(vk.device, memory, 0, buffer_size, 0, &mapped);
}

static void CreateMappedUniformBuffer(VkDescriptorSet descriptor_set, uint32_t dst_binding, bool dynamic,
                                      uint32_t buffer_size, uint32_t range, VkBuffer& buffer, VkDeviceMemory& memory,
                                      void*& mapped) {
  CreateMappedBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer_size, buffer, memory, mapped);

  VkDescriptorBufferInfo descriptor_buffer_info{
      .buffer = buffer,
      .offset = 0,
      .range = range,
  };

  VkWriteDescriptorSet write_descriptor_set{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .dstBinding = dst_binding,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .pImageInfo = nullptr,
      .pBufferInfo = &descriptor_buffer_info,
      .pTexelBufferView = nullptr,
  };

  vkUpdateDescriptorSets(vk.device, 1, &write_descriptor_set, 0, nullptr);
}

static uint32_t CalcVertexBufferStride(Rp& rp) {
  uint32_t ret = 0;
  for (auto attr : rp.vert_buf.attrs) {
    ret += RpVertAttrSize(attr);
  }
  return ret;
}

void RpInit(Rp& rp) {
  rp.shader_stages.clear();

  rp.Init(rp);

  std::vector<VkDescriptorPoolSize> desc_pool_sizes;
  std::vector<VkDescriptorSetLayoutBinding> desc_layout_bindings;

  const uint32_t num_uniforms = rp.static_uniform.enabled + rp.dynamic_uniform.enabled;
  desc_pool_sizes.reserve(num_uniforms);

  if (rp.static_uniform.enabled) {
    desc_layout_bindings.emplace_back(VkDescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    });

    uint32_t descriptor_count = kVulkan_NumFramesInFlight;
    desc_pool_sizes.emplace_back(VkDescriptorPoolSize{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = descriptor_count,
    });
  }

  if (rp.dynamic_uniform.enabled) {
    desc_layout_bindings.emplace_back(VkDescriptorSetLayoutBinding{
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    });

    uint32_t descriptor_count = kVulkan_NumFramesInFlight;
    desc_pool_sizes.emplace_back(VkDescriptorPoolSize{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = descriptor_count,
    });
  }

  const VkDescriptorPoolCreateInfo descriptor_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = static_cast<uint32_t>(kVulkan_NumFramesInFlight),
      .poolSizeCount = static_cast<uint32_t>(desc_pool_sizes.size()),
      .pPoolSizes = desc_pool_sizes.data(),
  };

  VkDescriptorPool descriptor_pool;
  VK_CHECK(vkCreateDescriptorPool(vk.device, &descriptor_pool_create_info, nullptr, &descriptor_pool));

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(desc_layout_bindings.size()),
      .pBindings = desc_layout_bindings.data(),
  };

  VkDescriptorSetLayout descriptor_set_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

  const VkPushConstantRange push_constant_range{
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = kPushConstantMaxSize,
  };

  const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = static_cast<uint32_t>(num_uniforms ? 1 : 0),
      .pSetLayouts = &descriptor_set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant_range,
  };

  vkCreatePipelineLayout(vk.device, &pipeline_layout_create_info, nullptr, &rp.layout);

  const std::vector<VkDescriptorSetLayout> descriptor_sets_layouts(kVulkan_NumFramesInFlight, descriptor_set_layout);

  const VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = static_cast<uint32_t>(descriptor_sets_layouts.size()),
      .pSetLayouts = descriptor_sets_layouts.data(),
  };

  rp.desc_sets.resize(kVulkan_NumFramesInFlight);
  VK_CHECK(vkAllocateDescriptorSets(vk.device, &descriptor_set_alloc_info, rp.desc_sets.data()));

  auto resize = [](auto& b) {
    if (!b.enabled) return;

    b.buffer.resize(kVulkan_NumFramesInFlight);
    b.mem.resize(kVulkan_NumFramesInFlight);
    b.mem_mapped.resize(kVulkan_NumFramesInFlight);
  };

  resize(rp.static_uniform);
  resize(rp.dynamic_uniform);
  resize(rp.vert_buf);

  for (size_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
    if (rp.static_uniform.enabled) {
      RpBuf& b = rp.static_uniform;
      CreateMappedUniformBuffer(rp.desc_sets[i], 0, (bool)false, b.size, b.range, b.buffer[i], b.mem[i],
                                reinterpret_cast<void*&>(b.mem_mapped[i]));
    }

    if (rp.dynamic_uniform.enabled) {
      RpBuf& b = rp.dynamic_uniform;
      CreateMappedUniformBuffer(rp.desc_sets[i], 1, true, b.size, b.range, b.buffer[i], b.mem[i],
                                reinterpret_cast<void*&>(b.mem_mapped[i]));
    }

    if (rp.vert_buf.enabled) {
      RpBuf& b = rp.vert_buf;
      CreateMappedBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, b.size, b.buffer[i], b.mem[i],
                         reinterpret_cast<void*&>(b.mem_mapped[i]));
    }
  }

  VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  if (rp.vert_buf.enabled) {
    auto& binding_description = Tmake<VkVertexInputBindingDescription>({
        .binding = 0,
        .stride = CalcVertexBufferStride(rp),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    });

    auto vertex_attr_descriptions = Tarr<VkVertexInputAttributeDescription>(rp.vert_buf.attrs.size());

    uint32_t offset = 0;
    for (uint32_t i = 0; i < rp.vert_buf.attrs.size(); ++i) {
      auto attr = rp.vert_buf.attrs[i];
      vertex_attr_descriptions[i] = {
          .location = i,
          .binding = 0,
          .format = RpVertAttrVkFormat(attr),
          .offset = offset,
      };
      offset += RpVertAttrSize(attr);
    }

    vertex_input_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_create_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_create_info.vertexAttributeDescriptionCount = vertex_attr_descriptions.size();
    vertex_input_create_info.pVertexAttributeDescriptions = vertex_attr_descriptions.data();
  }

  const VkPipelineRasterizationStateCreateInfo rasterizer_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = rp.disable_culling ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
  };

  rp.pipeline =
      Vulkan_CreateGraphicsPipeline(vertex_input_create_info, rp.layout, rp.shader_stages, rasterizer_create_info);
}

void RpBegin(Rp& rp) {
  rp.static_uniform.written = 0;
  rp.dynamic_uniform.written = 0;
  rp.vert_buf.written = 0;

  const VkCommandBuffer command_buffer = vk.command_buffers[vk.current_frame_data.iframe];
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rp.pipeline);
}

static void RpBufCopy(RpBuf& buf, CharView data) {
  CHECK(buf.enabled);
  CHECK(!data.empty());
  CHECK_LE(buf.written + data.size(), buf.size);

  void* dst = buf.mem_mapped[vk.current_frame_data.iframe] + buf.written;
  memcpy(dst, data.data(), data.size());
  buf.written += data.size();
}

void RpStaticUniformCopy(Rp& rp, CharView data) {
  CHECK_EQ(rp.static_uniform.size, data.size());
  RpBufCopy(rp.static_uniform, data);
}

RpMesh RpVertCopy(Rp& rp, uint32_t vert_count, CharView vert_data) {
  const uint32_t offset = rp.vert_buf.written;
  const uint32_t size = vert_count * CalcVertexBufferStride(rp);
  CHECK_EQ(vert_data.size(), size);

  RpBufCopy(rp.vert_buf, vert_data);

  return {offset, vert_count};
}

void RpPushConst(Rp& rp, CharView data) {
  CHECK(!data.empty());
  CHECK_LE(data.size(), kPushConstantMaxSize);

  const VkCommandBuffer cmd = vk.command_buffers[vk.current_frame_data.iframe];
  vkCmdPushConstants(cmd, rp.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, data.size(),
                     data.data());
}

void RpDraw(Rp& rp, RpMesh mesh, CharView dynamic_uniform_data) {
  const VkCommandBuffer cmd = vk.command_buffers[vk.current_frame_data.iframe];

  if (rp.vert_buf.enabled) {
    VkBuffer buf = rp.vert_buf.buffer[vk.current_frame_data.iframe];
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &mesh.offset);
  }

  if (rp.dynamic_uniform.enabled || rp.static_uniform.enabled) {
    uint32_t offset_count = 0;
    uint32_t offset = 0;

    if (rp.dynamic_uniform.enabled) {
      offset_count = 1;
      offset = rp.dynamic_uniform.written;

      rp.dynamic_uniform.written =
          AlignUp(rp.dynamic_uniform.written, vk.phys_device_props.limits.minUniformBufferOffsetAlignment);
      RpBufCopy(rp.dynamic_uniform, dynamic_uniform_data);
    }

    const VkDescriptorSet desc_set = rp.desc_sets[vk.current_frame_data.iframe];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rp.layout, 0, 1, &desc_set, offset_count, &offset);
  }

  vkCmdDraw(cmd, mesh.vert_count, 1, 0, 0);
}

VkFormat RpVertAttrVkFormat(RpVertAttr attr) {
  switch (attr) {
    case RpVertAttr::Float4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RpVertAttr::Half2:
      return VK_FORMAT_R16G16_SFLOAT;
    case RpVertAttr::SNorm8x4:
      return VK_FORMAT_R8G8B8A8_SNORM;
    case RpVertAttr::UNorm8x4:
      return VK_FORMAT_R8G8B8A8_UNORM;
  }

  CHECK(false);
  return VK_FORMAT_UNDEFINED;
}

uint32_t RpVertAttrSize(RpVertAttr attr) {
  switch (attr) {
    case RpVertAttr::Float4:
      return 16;
    case RpVertAttr::Half2:
      return 4;
    case RpVertAttr::SNorm8x4:
      return 4;
    case RpVertAttr::UNorm8x4:
      return 4;
  }

  CHECK(false);
  return 0;
}

void RpAttachVertShader(Rp& rp, const std::string& path) {
  rp.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile(path)),
      .pName = "main",
  });
}

void RpAttachFragShader(Rp& rp, const std::string& path) {
  rp.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile(path)),
      .pName = "main",
  });
}

}  // namespace nyla