#include "nyla/engine0/shader_manager.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/engine0/engine0_internal.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"
#include <cstdint>
#include <format>
#include <unistd.h>

namespace nyla
{

using namespace engine0_internal;

namespace
{

auto CreateShader(std::string_view name) -> RhiShader
{
    const std::string path = std::format("nyla/shaders/build/{}.hlsl.spv", name);
    const std::vector<std::byte> code = ReadFile(path);
    const auto spirv = std::span{reinterpret_cast<const uint32_t *>(code.data()), code.size() / 4};

    RhiShader shader = RhiCreateShader(RhiShaderDesc{
        .spirv = spirv,
    });
    return shader;
}

struct VertexInput
{
    float4 pos;
    float4 color;
};

struct PerDrawUbo
{
    float4 color;
};

struct CBuffer
{
    float4x4 vp;
    float4x4 invVp;
};

struct Renderer
{
    RhiGraphicsPipeline pipeline;
    RhiBindGroupLayout bindGroupLayout;
    RhiBindGroup bindGroup;
};

auto CreateRenderer() -> Renderer
{
    const RhiBindGroupLayout bindGroupLayout = RhiCreateBindGroupLayout(RhiBindGroupLayoutDesc{
        .bindingCount = 1,
        .bindings =
            {
                RhiBindingDesc{
                    .binding = 0,
                    .type = RhiBindingType::UniformBufferDynamic,
                    .arraySize = 1,
                    .stageFlags = RhiShaderStage::Vertex,
                },
            },
    });

    const RhiShader vs = CreateShader("world.ps");
    const RhiShader ps = CreateShader("world.vs");

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "E0Solid",
        .vs = vs,
        .ps = ps,
        .bindGroupLayoutsCount = 1,
        .bindGroupLayouts =
            {
                bindGroupLayout,
            },
        .vertexBindingsCount = 1,
        .vertexBindings =
            {
                RhiVertexBindingDesc{
                    .binding = 0,
                    .stride = sizeof(VertexInput),
                    .inputRate = RhiInputRate::PerVertex,
                },
            },
        .vertexAttributeCount = 2,
        .vertexAttributes =
            {
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .location = 0,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VertexInput, pos),
                },
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .location = 1,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VertexInput, color),
                },
            },
        .colorTargetFormatsCount = 1,
        .colorTargetFormats =
            {
                RhiGetTextureInfo(RhiGetBackbufferTexture()).format,
            },
        .pushConstantSize = sizeof(CBuffer),
        .cullMode = RhiCullMode::None,
        .frontFace = RhiFrontFace::CCW,
    };

    const RhiGraphicsPipeline pipeline = RhiCreateGraphicsPipeline(pipelineDesc);

    constexpr uint32_t kVertexBufferSize = 1 << 20;
    const RhiBuffer vertexBuffer = RhiCreateBuffer(RhiBufferDesc{
        .size = kVertexBufferSize,
        .bufferUsage = RhiBufferUsage::Vertex,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });

    constexpr uint32_t kPerDrawBufferSize = 1 << 10;
    const RhiBuffer perDrawBuffer = RhiCreateBuffer(RhiBufferDesc{
        .size = kPerDrawBufferSize,
        .bufferUsage = RhiBufferUsage::Uniform,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });

    const RhiBindGroup bindGroup = RhiCreateBindGroup(RhiBindGroupDesc{
        .layout = bindGroupLayout,
        .entriesCount = 1,
        .entries =
            {
                RhiBindGroupEntry{
                    .binding = 0,
                    .arrayIndex = 0,
                    .type = RhiBindingType::UniformBufferDynamic,
                    .buffer =
                        RhiBufferBinding{
                            .buffer = perDrawBuffer,
                            .size = kPerDrawBufferSize,
                            .offset = 0,
                            .range = sizeof(PerDrawUbo),
                        },
                },
            },
    });

    return Renderer{
        .pipeline = pipeline,
        .bindGroupLayout = bindGroupLayout,
        .bindGroup = bindGroup,
    };
}

} // namespace

} // namespace nyla