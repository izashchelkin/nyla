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
    RhiShaderStage stage_flags;
    uint32_t size;
    uint32_t range;
    uint32_t written;
    std::vector<RhiVertexFormat> attrs;
    RhiBuffer buffer[rhi_max_num_frames_in_flight];
};

inline RhiShaderStage RpBufStageFlags(const RpBuf &buf)
{
    if (Any(buf.stage_flags))
        return buf.stage_flags;
    else
        return RhiShaderStage::Vertex | RhiShaderStage::Fragment;
}

struct Rp
{
    std::string debug_name;
    RhiGraphicsPipeline pipeline;
    RhiBindGroupLayout bind_group_layout;
    RhiBindGroup bind_group[rhi_max_num_frames_in_flight];

    RhiShader vertex_shader;
    RhiShader fragment_shader;

    bool disable_culling;
    RpBuf static_uniform;
    RpBuf dynamic_uniform;
    RpBuf vert_buf;

    void (*Init)(Rp &);
};

struct RpMesh
{
    uint32_t offset;
    uint32_t vert_count;
};

void RpInit(Rp &rp);
void RpAttachVertShader(Rp &rp, const std::string &path);
void RpAttachFragShader(Rp &rp, const std::string &path);
void RpBegin(Rp &rp);
void RpPushConst(Rp &rp, CharView data);
void RpStaticUniformCopy(Rp &rp, CharView data);
RpMesh RpVertCopy(Rp &rp, uint32_t vert_count, CharView vert_data);
void RpDraw(Rp &rp, RpMesh mesh, CharView dynamic_uniform_data);

} // namespace nyla