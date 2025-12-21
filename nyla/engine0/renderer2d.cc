#include "nyla/engine0/renderer2d.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/memory/align.h"
#include "nyla/engine0/engine0_internal.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"
#include <cstdint>
#include <sys/types.h>
#include <unistd.h>

namespace nyla
{

using namespace engine0_internal;

namespace
{

struct VSInput
{
    float4 pos;
    float4 color;
};

struct EntityUbo
{
    float4x4 model;
    float4 color;
};

struct Scene
{
    float4x4 vp;
    float4x4 invVp;
};

} // namespace

struct Renderer2D
{
    RhiGraphicsPipeline pipeline;
    RhiBindGroupLayout bindGroupLayout;
    RhiBuffer vertexBuffer;
    std::array<RhiBindGroup, kRhiMaxNumFramesInFlight> bindGroup;
    std::array<RhiBuffer, kRhiMaxNumFramesInFlight> dynamicUniformBuffer;
    uint32_t dymamicUniformBufferWritten;
};

auto CreateRenderer2D() -> Renderer2D *
{
    const RhiShader vs = GetShader("renderer2d.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("renderer2d.ps", RhiShaderStage::Pixel);

    auto *renderer = new Renderer2D{};

    renderer->bindGroupLayout = RhiCreateBindGroupLayout(RhiBindGroupLayoutDesc{
        .bindingCount = 1,
        .bindings =
            {
                RhiBindingDesc{
                    .binding = 0,
                    .type = RhiBindingType::UniformBuffer,
                    .flags = RhiBindingFlags::Dynamic,
                    .arraySize = 1,
                    .stageFlags = RhiShaderStage::Vertex | RhiShaderStage::Pixel,
                },
            },
    });

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "Renderer2D",
        .vs = vs,
        .ps = ps,
        .bindGroupLayoutsCount = 1,
        .bindGroupLayouts =
            {
                renderer->bindGroupLayout,
            },
        .vertexBindingsCount = 1,
        .vertexBindings =
            {
                RhiVertexBindingDesc{
                    .binding = 0,
                    .stride = sizeof(VSInput),
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
                    .offset = offsetof(VSInput, pos),
                },
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .location = 1,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VSInput, color),
                },
            },
        .colorTargetFormatsCount = 1,
        .colorTargetFormats =
            {
                RhiGetTextureInfo(RhiGetBackbufferTexture()).format,
            },
        .pushConstantSize = sizeof(Scene),
        .cullMode = RhiCullMode::None,
        .frontFace = RhiFrontFace::CCW,
    };

    renderer->pipeline = RhiCreateGraphicsPipeline(pipelineDesc);

    constexpr uint32_t kVertexBufferSize = 1 << 20;
    renderer->vertexBuffer = RhiCreateBuffer(RhiBufferDesc{
        .size = kVertexBufferSize,
        .bufferUsage = RhiBufferUsage::Vertex | RhiBufferUsage::CopyDst,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
    });

    for (uint32_t i = 0; i < RhiGetNumFramesInFlight(); ++i)
    {
        constexpr uint32_t kDynamicUniformBufferSize = 1 << 20;
        renderer->dynamicUniformBuffer[i] = RhiCreateBuffer(RhiBufferDesc{
            .size = kDynamicUniformBufferSize,
            .bufferUsage = RhiBufferUsage::Uniform,
            .memoryUsage = RhiMemoryUsage::CpuToGpu,
        });

        renderer->bindGroup[i] = RhiCreateBindGroup(RhiBindGroupDesc{
            .layout = renderer->bindGroupLayout,
            .entriesCount = 1,
            .entries =
                {
                    RhiBindGroupEntry{
                        .binding = 0,
                        .arrayIndex = 0,
                        .buffer =
                            RhiBufferBinding{
                                .buffer = renderer->dynamicUniformBuffer[i],
                                .size = kDynamicUniformBufferSize,
                                .offset = 0,
                                .range = sizeof(EntityUbo),
                            },
                    },
                },
        });
    }

    return renderer;
}

void Renderer2DFrameBegin(RhiCmdList cmd, Renderer2D *renderer, StagingBuffer *stagingBuffer)
{
    static bool uploadedVertices = false;
    if (!uploadedVertices)
    {
        RhiCmdTransitionBuffer(cmd, renderer->vertexBuffer, RhiBufferState::CopyDst);

        char *uploadMemory = E0AcquireUploadMemory(cmd, stagingBuffer, renderer->vertexBuffer, 0, 6 * sizeof(VSInput));
        new (uploadMemory) std::array<VSInput, 6>{
            VSInput{
                .pos = {-.5f, .5f, .0f, 1.f},
                .color = {},
            },
            VSInput{
                .pos = {.5f, -.5f, .0f, 1.f},
                .color = {},
            },
            VSInput{
                .pos = {.5f, .5f, .0f, 1.f},
                .color = {},
            },

            VSInput{
                .pos = {-.5f, .5f, .0f, 1.f},
                .color = {},
            },
            VSInput{
                .pos = {-.5f, -.5f, .0f, 1.f},
                .color = {},
            },
            VSInput{
                .pos = {.5f, -.5f, .0f, 1.f},
                .color = {},
            },
        };

        RhiCmdTransitionBuffer(cmd, renderer->vertexBuffer, RhiBufferState::ShaderRead);

        uploadedVertices = true;
    }
}

void Renderer2DPassBegin(RhiCmdList cmd, Renderer2D *renderer, uint32_t width, uint32_t height, float metersOnScreen)
{
    RhiCmdBindGraphicsPipeline(cmd, renderer->pipeline);

    float worldW;
    float worldH;

    const float base = metersOnScreen;
    const float aspect = ((float)width) / ((float)height);

    worldH = base;
    worldW = base * aspect;

    float4x4 view = float4x4::Identity();
    float4x4 proj = float4x4::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);

    float4x4 vp = proj.Mult(view);
    float4x4 invVp = vp.Inversed();
    Scene scene = {
        .vp = vp,
        .invVp = invVp,
    };

    RhiCmdPushGraphicsConstants(cmd, 0, RhiShaderStage::Vertex | RhiShaderStage::Pixel, ByteViewPtr(&scene));

    std::array<RhiBuffer, 1> buffers{renderer->vertexBuffer};
    std::array<uint32_t, 1> offsets{0};

    RhiCmdBindVertexBuffers(cmd, 0, buffers, offsets);

    renderer->dymamicUniformBufferWritten = 0;
}

void Renderer2DDrawRect(RhiCmdList cmd, Renderer2D *renderer, float x, float y, float width, float height, float4 color)
{
    const uint32_t frameIndex = RhiGetFrameIndex();

    AlignUp(renderer->dymamicUniformBufferWritten, RhiGetMinUniformBufferOffsetAlignment());
    new (RhiMapBuffer(renderer->dynamicUniformBuffer[frameIndex]) + renderer->dymamicUniformBufferWritten) EntityUbo{
        .model = float4x4::Translate(float4{x, y, 0, 1}).Mult(float4x4::Scale(float4{width, height, 1, 1})),
        .color = color,
    };

    uint32_t offset = renderer->dymamicUniformBufferWritten;
    renderer->dymamicUniformBufferWritten += sizeof(EntityUbo);

    RhiCmdBindGraphicsBindGroup(cmd, 0, renderer->bindGroup[frameIndex], {&offset, 1});
    RhiCmdDraw(cmd, 6, 1, 0, 0);
}

} // namespace nyla