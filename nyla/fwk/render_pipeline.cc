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

    auto init_buffer = [&bind_group_layout_desc, &bind_group_desc](
                           RpBuf& buf, RhiBufferUsage buffer_usage, RhiMemoryUsage memory_usage,
                           RhiBindingType binding_type, uint32_t binding, uint32_t i) {
      bind_group_layout_desc.bindings[i] = {
          .binding = binding,
          .type = binding_type,
          .array_size = 1,
          .stage_flags = RpBufStageFlags(buf),
      };

      for (uint32_t iframe = 0; iframe < vk.frames_inflight; ++iframe) {
        buf.buffer[iframe] = RhiCreateBuffer(RhiBufferDesc{
            .size = buf.size,
            .buffer_usage = buffer_usage,
            .memory_usage = memory_usage,
        });

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
      init_buffer(rp.static_uniform, RhiBufferUsage::Uniform, RhiMemoryUsage::CpuToGpu, RhiBindingType::UniformBuffer,
                  0, i++);
    }
    if (rp.dynamic_uniform.enabled) {
      init_buffer(rp.dynamic_uniform, RhiBufferUsage::Uniform, RhiMemoryUsage::CpuToGpu,
                  RhiBindingType::UniformBufferDynamic, 1, i++);
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
}  // namespace nyla

void RpBegin(Rp& rp, RhiCmdList cmd) {
  rp.static_uniform.written = 0;
  rp.dynamic_uniform.written = 0;
  rp.vert_buf.written = 0;

  RhiCmdBindGraphicsPipeline(cmd, rp.pipeline);
}

static void RpBufCopy(RpBuf& buf, CharView data) {
  CHECK(buf.enabled);
  CHECK(!data.empty());
  CHECK_LE(buf.written + data.size(), buf.size);

  void* dst = (char*)RhiMapBuffer(buf.buffer[RhiFrameGetIndex()], true) + buf.written;
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
  CHECK_LE(data.size(), rhi_max_push_constant_size);

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