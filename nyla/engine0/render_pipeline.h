#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nyla/commons/memory/charview.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_pipeline.h"

namespace nyla
{

struct RpBuf
{
    bool enabled;
    RhiShaderStage stageFlags;
    uint32_t size;
    uint32_t range;
    uint32_t written;
    std::vector<RhiVertexFormat> attrs;
    std::array<RhiBuffer, kRhiMaxNumFramesInFlight> buffer;
};

inline auto RpBufStageFlags(const RpBuf &buf) -> RhiShaderStage
{
    if (Any(buf.stageFlags))
        return buf.stageFlags;
    else
        return RhiShaderStage::Vertex | RhiShaderStage::Pixel;
}

struct Rp
{
    std::string debugName;
    RhiGraphicsPipeline pipeline;
    RhiBindGroupLayout bindGroupLayout;
    std::array<RhiBindGroup, kRhiMaxNumFramesInFlight> bindGroup;

    RhiShader vertexShader;
    RhiShader fragmentShader;

    bool disableCulling;
    RpBuf staticUniform;
    RpBuf dynamicUniform;
    RpBuf vertBuf;

    void (*init)(Rp &);
};

struct RpMesh
{
    uint32_t offset;
    uint32_t vertCount;
};

void RpInit(Rp &rp);
void RpAttachVertShader(Rp &rp, const std::string &path);
void RpAttachFragShader(Rp &rp, const std::string &path);
void RpBegin(Rp &rp);
void RpPushConst(Rp &rp, ByteView data);
void RpStaticUniformCopy(Rp &rp, ByteView data);
auto RpVertCopy(Rp &rp, uint32_t vertCount, ByteView vertData) -> RpMesh;
void RpDraw(Rp &rp, RpMesh mesh, ByteView dynamicUniformData);

} // namespace nyla