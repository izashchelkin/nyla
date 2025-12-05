#pragma once

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_shader.h"

namespace nyla
{

constexpr inline uint32_t rhi_max_bind_group_layouts = 4;

struct RhiBindGroup : RhiHandle
{
};
struct RhiBindGroupLayout : RhiHandle
{
};

enum class RhiBindingType
{
    UniformBuffer,
    UniformBufferDynamic
};

struct RhiBufferBinding
{
    RhiBuffer buffer;
    uint32_t offset;
    uint32_t size;
};

struct RhiBindGroupEntry
{
    uint32_t binding;
    RhiBindingType type;
    uint32_t array_index;
    union {
        RhiBufferBinding buffer;
    };
};

struct RhiBindGroupDesc
{
    RhiBindGroupLayout layout;
    RhiBindGroupEntry entries[4];
    uint32_t entries_count;
};

struct RhiBindingDesc
{
    uint32_t binding;
    RhiBindingType type;
    uint32_t array_size;
    RhiShaderStage stage_flags;
};

struct RhiBindGroupLayoutDesc
{
    RhiBindingDesc bindings[16];
    uint32_t binding_count;
};

auto RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc &) -> RhiBindGroupLayout;
void RhiDestroyBindGroupLayout(RhiBindGroupLayout);

auto RhiCreateBindGroup(const RhiBindGroupDesc &) -> RhiBindGroup;
void RhiDestroyBindGroup(RhiBindGroup);

} // namespace nyla