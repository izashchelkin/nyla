#include "nyla/fwk/render_pipeline.h"

#include <unistd.h>

#include <cstdint>

#include "absl/strings/str_format.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/commons/memory/align.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_handle_pool.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

namespace {

void CreateMappedBuffer(RhiBufferUsage buffer_usage, uint32_t buffer_size, void*& mapped) {
  RhiBuffer buffer = RhiCreateBuffer(RhiBufferDesc{
      .size = buffer_size,
      .buffer_usage = buffer_usage,
      .memory_usage = RhiMemoryUsage::CpuToGpu,
  });
  mapped = RhiMapBuffer(buffer);
}

uint32_t GetVertAttrSize(RhiVertexAttributeType attr) {
  switch (attr) {
    case RhiVertexAttributeType::Float4:
      return 16;
    case RhiVertexAttributeType::Half2:
      return 4;
    case RhiVertexAttributeType::SNorm8x4:
      return 4;
    case RhiVertexAttributeType::UNorm8x4:
      return 4;
  }
  CHECK(false);
  return 0;
}

RhiFormat GetVertAttrFormat(RhiVertexAttributeType attr) {
  switch (attr) {
    case RhiVertexAttributeType::Float4:
      return RhiFormat::Float4;
    case RhiVertexAttributeType::Half2:
      return RhiFormat::Half2;
    case RhiVertexAttributeType::SNorm8x4:
      return RhiFormat::SNorm8x4;
    case RhiVertexAttributeType::UNorm8x4:
      return RhiFormat::UNorm8x4;
  }
  CHECK(false);
  return static_cast<RhiFormat>(0);
}

uint32_t GetVertBindingStride(Rp& rp) {
  uint32_t ret = 0;
  for (auto attr : rp.vert_buf.attrs) {
    ret += GetVertAttrSize(attr);
  }
  return ret;
}

}  // namespace

static void UpdateDescriptorSet(VkDescriptorSet descriptor_set, uint32_t dst_binding, bool dynamic, uint32_t range,
                                VkBuffer& buffer) {
  const VkDescriptorBufferInfo descriptor_buffer_info{
      .buffer = buffer,
      .offset = 0,
      .range = range,
  };

  const VkWriteDescriptorSet write_descriptor_set{
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

// static void RpBufInit(Rp& rp, VkImageUsageFlags usage, RpBuf& b, const char* name, auto visitor) {
//   if (!b.enabled) return;
//
//   b.buffer.reserve(kVulkan_NumFramesInFlight);
//   b.mem.reserve(kVulkan_NumFramesInFlight);
//   b.mem_mapped.reserve(kVulkan_NumFramesInFlight);
//
//   for (size_t i = 0; i < kVulkan_NumFramesInFlight; ++i) {
//     if (b.buffer.size() <= i) {
//       b.buffer.resize(i + 1);
//       b.mem.resize(i + 1);
//       b.mem_mapped.resize(i + 1);
//
//       CreateMappedBuffer(usage, b.size, b.buffer[i], b.mem[i], reinterpret_cast<void*&>(b.mem_mapped[i]));
//       VulkanNameHandle(b.buffer[i], absl::StrFormat("Rp %s %s %d", rp.name, name, i));
//     }
//
//     visitor(rp, i, b);
//   }
// }

void RpInit(Rp& rp) {
  CHECK(!rp.debug_name.empty());

  if (RhiHandleIsSet(rp.pipeline)) {
    RhiDestroyGraphicsPipeline(rp.pipeline);
  } else {
    RhiBindGroupLayoutDesc bind_group_layout_desc{};
    bind_group_layout_desc.binding_count = rp.static_uniform.enabled + rp.dynamic_uniform.enabled;

    RhiBindGroupDesc bind_group_desc[rhi_max_num_frames_in_flight]{};

    for (uint32_t j = 0; j < vk.frames_inflight; ++j) {
      bind_group_desc[j].entries_count = bind_group_layout_desc.binding_count;
    }

    auto process_buffer_binding = [&bind_group_layout_desc, &bind_group_desc](RpBuf buf, RhiBindingType binding_type,
                                                                              uint32_t binding, uint32_t i) {
      bind_group_layout_desc.bindings[i] = {
          .binding = binding,
          .type = binding_type,
          .array_size = 1,
          .stage_flags = RpBufStageFlags(buf),
      };

      for (uint32_t iframe = 0; iframe < vk.frames_inflight; ++iframe) {
        bind_group_desc[iframe].entries[i] = RhiBindGroupEntry{
            .binding = binding,
            .type = binding_type,
            .array_index = 0,
            .buffer =
                {
                    .buffer = buf.buffer[iframe],
                    .offset = 0,
                    .size = buf.size,
                },
        };
      }
    };

    uint32_t i = 0;
    if (rp.static_uniform.enabled) {
      process_buffer_binding(rp.static_uniform, RhiBindingType::UniformBuffer, 0, i++);
    }
    if (rp.dynamic_uniform.enabled) {
      process_buffer_binding(rp.dynamic_uniform, RhiBindingType::UniformBufferDynamic, 1, i++);
    }

    rp.bind_group_layout = RhiCreateBindGroupLayout(bind_group_layout_desc);

    for (uint32_t iframe = 0; iframe < vk.frames_inflight; ++iframe) {
      bind_group_desc[iframe].layout = rp.bind_group_layout;
      rp.bind_group[iframe] = RhiCreateBindGroup(bind_group_desc[iframe]);
    }
  }

  RhiGraphicsPipelineDesc desc{
      .debug_name = absl::StrFormat("Rp %v", rp.debug_name),
      .cull_mode = rp.disable_culling ? RhiCullMode::None : RhiCullMode::Back,
      .front_face = RhiFrontFace::CCW,
  };

  desc.bind_group_layouts_count = 1;
  desc.bind_group_layouts[0] = rp.bind_group_layout;

  desc.vertex_bindings_count = rp.vert_buf.attrs.size();
  desc.vertex_attribute_count = rp.vert_buf.attrs.size();

  {
    uint32_t offset = 0;
    for (uint32_t i = 0; i < rp.vert_buf.attrs.size(); ++i) {
      auto attr = rp.vert_buf.attrs[i];

      desc.vertex_bindings[i] = RhiVertexBindingDesc{
          .binding = 0,
          .stride = GetVertBindingStride(rp),
          .input_rate = RhiInputRate::PerVertex,
      };

      RhiFormat format = GetVertAttrFormat(attr);
      desc.vertex_attributes[i] = RhiVertexAttributeDesc{
          .location = i,
          .binding = 0,
          .format = format,
          .offset = offset,
      };

      offset += GetVertAttrSize(attr);
    }
  }

  rp.Init(rp);
  RhiCreateGraphicsPipeline(desc);

  //

  rp.shader_stages.clear();

  std::vector<VkDescriptorPoolSize> desc_pool_sizes;
  std::vector<VkDescriptorSetLayoutBinding> desc_layout_bindings;

  const uint32_t num_uniforms = rp.static_uniform.enabled + rp.dynamic_uniform.enabled;
  desc_pool_sizes.reserve(num_uniforms);

  if (rp.static_uniform.enabled) {
    desc_layout_bindings.emplace_back(VkDescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = RpBufStageFlags(rp.static_uniform),
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
        .stageFlags = RpBufStageFlags(rp.dynamic_uniform),
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

  RpBufInit(rp, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, rp.static_uniform, "Static Uniform",
            [](Rp& rp, size_t i, RpBuf& b) {
              UpdateDescriptorSet(rp.desc_sets[i], 0, false, b.range, b.buffer[i]);  //
            });
  RpBufInit(rp, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, rp.dynamic_uniform, "Dynamic Uniform",
            [](Rp& rp, size_t i, RpBuf& b) {
              UpdateDescriptorSet(rp.desc_sets[i], 1, true, b.range, b.buffer[i]);  //
            });
  RpBufInit(rp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, rp.vert_buf, "Vertex Buffer",  //
            [](Rp& rp, size_t i, RpBuf& b) {
              //
            });

  VkPipelineVertexInputStateCreateInfo vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  if (rp.vert_buf.enabled) {
    auto& binding_description = Tmake<VkVertexInputBindingDescription>({
        .binding = 0,
        .stride = GetVertBindingStride(rp),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    });

    auto vertex_attr_descriptions = Tmakearr<VkVertexInputAttributeDescription>(rp.vert_buf.attrs.size());

    uint32_t offset = 0;
    for (uint32_t i = 0; i < rp.vert_buf.attrs.size(); ++i) {
      auto attr = rp.vert_buf.attrs[i];
      vertex_attr_descriptions[i] = {
          .location = i,
          .binding = 0,
          .format = GetVertAttrFormat(attr),
          .offset = offset,
      };
      offset += GetVertAttrSize(attr);
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
  VulkanNameHandle(rp.pipeline, absl::StrFormat("Rp %v", rp.name));
}  // namespace nyla

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
  const uint32_t size = vert_count * GetVertBindingStride(rp);
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
      rp.dynamic_uniform.written =
          AlignUp(rp.dynamic_uniform.written, vk.phys_device_props.limits.minUniformBufferOffsetAlignment);

      offset_count = 1;
      offset = rp.dynamic_uniform.written;
      RpBufCopy(rp.dynamic_uniform, dynamic_uniform_data);
    }

    const VkDescriptorSet desc_set = rp.desc_sets[vk.current_frame_data.iframe];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rp.layout, 0, 1, &desc_set, offset_count, &offset);
  }

  vkCmdDraw(cmd, mesh.vert_count, 1, 0, 0);
}

void RpAttachVertShader(Rp& rp, const std::string& path) {
  rp.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile(path)),
      .pName = "main",
  });

  PlatformFsWatch(path);
}

void RpAttachFragShader(Rp& rp, const std::string& path) {
  rp.shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = Vulkan_CreateShaderModule(ReadFile(path)),
      .pName = "main",
  });

  PlatformFsWatch(path);
}

}  // namespace nyla