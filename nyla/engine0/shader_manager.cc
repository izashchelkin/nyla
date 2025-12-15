#include "nyla/engine0/shader_manager.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/engine0/engine0_internal.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/spirview/spirview.h"
#include <cstdint>
#include <format>

namespace nyla
{

using namespace engine0_internal;

auto E0GetShader(std::string_view name) -> E0Shader
{
    for (uint32_t i = 0; i < e0Handles.shaders.slots.size(); ++i)
    {
        const auto &shaderSlot = e0Handles.shaders.slots[i];
        if (shaderSlot.used && shaderSlot.data.name == name)
        {
            return static_cast<E0Shader>(Handle{
                .gen = shaderSlot.gen,
                .index = i,
            });
        }
    }

    const std::string path = std::format("nyla/shaders/build/{}.hlsl.spv", name);
    const std::vector<std::byte> code = ReadFile(path);
    const auto spirv = std::span{reinterpret_cast<const uint32_t *>(code.data()), code.size() / 4};

    E0ShaderData shaderData{
        .name = name,
        .shader = RhiCreateShader(RhiShaderDesc{
            .spirv = spirv,
        }),
    };
    SpirviewReflect(spirv, &shaderData.reflectResult);

    return HandleAcquire(e0Handles.shaders, shaderData);
}

struct E0SolidPipelineVertex
{
    float4 pos;
    float4 color;
};

auto E0SolidPipeline()
{
    RhiTextureInfo backbuffer = RhiGetTextureInfo(RhiGetBackbufferTexture());

    RhiBindGroupLayout bindGroupLayout = RhiCreateBindGroupLayout(RhiBindGroupLayoutDesc{
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

    E0Shader vs = E0GetShader("world.vs");
    E0Shader ps = E0GetShader("world.ps");

    RhiCreateGraphicsPipeline(RhiGraphicsPipelineDesc{
        .debugName = "E0Solid",
        .bindGroupLayoutsCount = 1,
        .bindGroupLayouts = {bindGroupLayout},
        .vertexBindingsCount = 1,
        .vertexBindings =
            {
                RhiVertexBindingDesc{
                    .binding = 0,
                    .stride = sizeof(E0SolidPipelineVertex),
                    .inputRate = RhiInputRate::PerVertex,
                },
            },
        .vertexAttributeCount = 2,
        .vertexAttributes =
            {
                RhiVertexAttributeDesc{
                    .location = 0,
                    .binding = 0,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(E0SolidPipelineVertex, pos),
                },
                RhiVertexAttributeDesc{
                    .location = 1,
                    .binding = 0,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(E0SolidPipelineVertex, color),
                },
            },
        .colorTargetFormatsCount = 1,
        .colorTargetFormats = {backbuffer.format},
        .cullMode = RhiCullMode::None,
        .frontFace = RhiFrontFace::CCW,
    });
}

} // namespace nyla