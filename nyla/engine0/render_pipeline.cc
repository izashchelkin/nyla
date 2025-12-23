#include "nyla/engine0/render_pipeline.h"

#include <unistd.h>

#include "absl/log/check.h"
#include "nyla/commons/os/readfile.h"
#include <cstdint>

#include "nyla/commons/handle.h"
#include "nyla/commons/memory/align.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/spirview/spirview.h"

namespace nyla
{

namespace
{

auto GetVertBindingStride(Rp &rp) -> uint32_t
{
    uint32_t ret = 0;
    for (auto attr : rp.vertBuf.attrs)
    {
        ret += RhiGetVertexFormatSize(attr);
    }
    return ret;
}

} // namespace

void RpInit(Rp &rp)
{
    CHECK(!rp.debugName.empty());

    if (HandleIsSet(rp.pipeline))
    {
        RhiDestroyGraphicsPipeline(rp.pipeline);
    }
    else
    {
        RhiBindGroupLayoutDesc bindGroupLayoutDesc{};
        bindGroupLayoutDesc.entriesCount = rp.staticUniform.enabled + rp.dynamicUniform.enabled;

        std::array<RhiBindGroupWriteDesc, kRhiMaxNumFramesInFlight> bindGroupDesc;

        for (uint32_t j = 0; j < RhiGetNumFramesInFlight(); ++j)
        {
            bindGroupDesc[j].entriesCount = bindGroupLayoutDesc.entriesCount;
        }

        if (rp.vertBuf.enabled)
        {
            for (uint32_t iframe = 0; iframe < RhiGetNumFramesInFlight(); ++iframe)
            {
                rp.vertBuf.buffer[iframe] = RhiCreateBuffer(RhiBufferDesc{
                    .size = rp.vertBuf.size,
                    .bufferUsage = RhiBufferUsage::Vertex,
                    .memoryUsage = RhiMemoryUsage::CpuToGpu,
                });
            }
        }

        auto initBuffer = [&bindGroupLayoutDesc, &bindGroupDesc](RpBuf &buf, RhiBufferUsage bufferUsage,
                                                                 RhiMemoryUsage memoryUsage, RhiBindingType bindingType,
                                                                 uint32_t binding, uint32_t i, bool dynamic) -> void {
            bindGroupLayoutDesc.entries[i] = {
                .binding = binding,
                .type = bindingType,
                .flags = dynamic ? RhiBindingFlags::Dynamic : static_cast<RhiBindingFlags>(0),
                .arraySize = 1,
                .stageFlags = RpBufStageFlags(buf),
            };

            for (uint32_t iframe = 0; iframe < RhiGetNumFramesInFlight(); ++iframe)
            {
                buf.buffer[iframe] = RhiCreateBuffer(RhiBufferDesc{
                    .size = buf.size,
                    .bufferUsage = bufferUsage,
                    .memoryUsage = memoryUsage,
                });

                bindGroupDesc[iframe].entries[i] = RhiBindGroupWriteEntry{
                    .binding = binding,
                    .arrayIndex = 0,
                    .buffer =
                        {
                            .buffer = buf.buffer[iframe],
                            .size = buf.size,
                            .offset = 0,
                            .range = buf.range,
                        },
                };
            }
        };

        uint32_t i = 0;
        if (rp.staticUniform.enabled)
        {
            initBuffer(rp.staticUniform, RhiBufferUsage::Uniform, RhiMemoryUsage::CpuToGpu,
                       RhiBindingType::UniformBuffer, 0, i++, false);
        }
        if (rp.dynamicUniform.enabled)
        {
            initBuffer(rp.dynamicUniform, RhiBufferUsage::Uniform, RhiMemoryUsage::CpuToGpu,
                       RhiBindingType::UniformBuffer, 1, i++, true);
        }

        rp.bindGroupLayout = RhiCreateBindGroupLayout(bindGroupLayoutDesc);

        for (uint32_t iframe = 0; iframe < RhiGetNumFramesInFlight(); ++iframe)
        {
            bindGroupDesc[iframe].layout = rp.bindGroupLayout;
            rp.bindGroup[iframe] = RhiCreateBindGroup(bindGroupDesc[iframe]);
        }
    }

    RhiTextureInfo backbuffer = RhiGetTextureInfo(RhiGetBackbufferTexture());

    RhiGraphicsPipelineDesc desc{
        .debugName = std::format("Rp %v", rp.debugName),
        .colorTargetFormats = {backbuffer.format},
        .colorTargetFormatsCount = 1,
        .cullMode = rp.disableCulling ? RhiCullMode::None : RhiCullMode::Back,
        .frontFace = RhiFrontFace::CCW,
    };

    desc.bindGroupLayoutsCount = 1;
    desc.bindGroupLayouts[0] = rp.bindGroupLayout;

    desc.vertexBindingsCount = 1;
    desc.vertexBindings[0] = RhiVertexBindingDesc{
        .binding = 0,
        .stride = GetVertBindingStride(rp),
        .inputRate = RhiInputRate::PerVertex,
    };

    desc.vertexAttributeCount = rp.vertBuf.attrs.size();

    uint32_t offset = 0;
    for (uint32_t i = 0; i < rp.vertBuf.attrs.size(); ++i)
    {
        RhiVertexFormat attr = rp.vertBuf.attrs[i];

        desc.vertexAttributes[i] = RhiVertexAttributeDesc{
            .location = i,
            .binding = 0,
            .format = attr,
            .offset = offset,
        };

        offset += RhiGetVertexFormatSize(attr);
    }

    rp.init(rp);

    desc.vs = rp.vertexShader;
    desc.ps = rp.fragmentShader;

    rp.pipeline = RhiCreateGraphicsPipeline(desc);
}

void RpBegin(Rp &rp)
{
    rp.staticUniform.written = 0;
    rp.dynamicUniform.written = 0;
    rp.vertBuf.written = 0;

    RhiCmdBindGraphicsPipeline(RhiFrameGetCmdList(), rp.pipeline);
}

static void RpBufCopy(RpBuf &buf, ByteView data)
{
    CHECK(buf.enabled);
    CHECK(!data.empty());
    CHECK_LE(buf.written + data.size(), buf.size);

    void *dst = (char *)RhiMapBuffer(buf.buffer[RhiGetFrameIndex()]) + buf.written;
    memcpy(dst, data.data(), data.size());
    buf.written += data.size();
}

void RpStaticUniformCopy(Rp &rp, ByteView data)
{
    CHECK_EQ(rp.staticUniform.size, data.size());
    RpBufCopy(rp.staticUniform, data);
}

auto RpVertCopy(Rp &rp, uint32_t vertCount, ByteView vertData) -> RpMesh
{
    const uint32_t offset = rp.vertBuf.written;
    const uint32_t size = vertCount * GetVertBindingStride(rp);
    CHECK_EQ(vertData.size(), size);

    RpBufCopy(rp.vertBuf, vertData);

    return {
        .offset = offset,
        .vertCount = vertCount,
    };
}

void RpPushConst(Rp &rp, ByteView data)
{
    CHECK(!data.empty());
    CHECK_LE(data.size(), kRhiMaxPushConstantSize);

    RhiCmdList cmd = RhiFrameGetCmdList();
    RhiCmdPushGraphicsConstants(cmd, 0, RhiShaderStage::Vertex | RhiShaderStage::Pixel, data);
}

void RpDraw(Rp &rp, RpMesh mesh, ByteView dynamicUniformData)
{
    RhiCmdList cmd = RhiFrameGetCmdList();
    uint32_t frameIndex = RhiGetFrameIndex();

    if (rp.vertBuf.enabled)
    {
        std::array<RhiBuffer, 1> buffers{rp.vertBuf.buffer[frameIndex]};
        std::array<uint32_t, 1> offsets{mesh.offset};

        RhiCmdBindVertexBuffers(cmd, 0, buffers, offsets);
    }

    if (rp.dynamicUniform.enabled || rp.staticUniform.enabled)
    {
        uint32_t offsetCount = 0;
        uint32_t offset = 0;

        if (rp.dynamicUniform.enabled)
        {
            rp.dynamicUniform.written = AlignedUp(rp.dynamicUniform.written, RhiGetMinUniformBufferOffsetAlignment());

            offsetCount = 1;
            offset = rp.dynamicUniform.written;
            RpBufCopy(rp.dynamicUniform, dynamicUniformData);
        }

        RhiCmdBindGraphicsBindGroup(cmd, 0, rp.bindGroup[frameIndex], {&offset, offsetCount});
    }

    RhiCmdDraw(cmd, mesh.vertCount, 1, 0, 0);
}

void RpAttachVertShader(Rp &rp, const std::string &path)
{
    if (HandleIsSet(rp.vertexShader))
    {
        RhiDestroyShader(rp.vertexShader);
    }

    const std::vector<std::byte> code = ReadFile(path);
    auto spirv = std::span{reinterpret_cast<const uint32_t *>(code.data()), code.size() / 4};

    SpirviewReflectResult result{};
    SpirviewReflect(spirv, &result);

    rp.vertexShader = RhiCreateShader(RhiShaderDesc{
        .spirv = spirv,
    });
    // PlatformFsWatch(path);
}

void RpAttachFragShader(Rp &rp, const std::string &path)
{
    if (HandleIsSet(rp.fragmentShader))
    {
        RhiDestroyShader(rp.fragmentShader);
    }

    const std::vector<std::byte> code = ReadFile(path);
    auto spirv = std::span{reinterpret_cast<const uint32_t *>(code.data()), code.size() / 4};

    SpirviewReflectResult result{};
    SpirviewReflect(spirv, &result);

    rp.fragmentShader = RhiCreateShader(RhiShaderDesc{
        .spirv = spirv,
    });
    // PlatformFsWatch(path);
}

} // namespace nyla