#include "nyla/commons/gpu_upload.h"

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

namespace
{

constexpr uint64_t kPerFrameUploadMaxSize = 16_MiB;

struct gpu_upload_manager_state
{
    rhi_buffer m_StagingBuffer;
    uint64_t m_StagingBufferAt;

    uint64_t m_StaticVertexBufferSize;
    uint64_t m_StaticVertexBufferAt;
    rhi_buffer m_StaticVertexBuffer;

    uint64_t m_StaticIndexBufferSize;
    uint64_t m_StaticIndexBufferAt;
    rhi_buffer m_StaticIndexBuffer;
};
gpu_upload_manager_state *g_State;

auto PrepareCopySrc(uint64_t copySize) -> uint64_t
{
    g_State->m_StagingBufferAt = AlignedUp(g_State->m_StagingBufferAt, Rhi::GetOptimalBufferCopyOffsetAlignment());
    ASSERT(g_State->m_StagingBufferAt + copySize <= kPerFrameUploadMaxSize);

    Rhi::BufferMarkWritten(g_State->m_StagingBuffer, g_State->m_StagingBufferAt, copySize);

    const uint64_t ret = kPerFrameUploadMaxSize * Rhi::GetFrameIndex() + g_State->m_StagingBufferAt;
    return ret;
}

} // namespace

namespace GpuUpload
{

void API Bootstrap()
{
    g_State = &RegionAlloc::Alloc<gpu_upload_manager_state>(RegionAlloc::g_BootstrapAlloc);

    g_State->m_StagingBufferAt = 0;
    g_State->m_StagingBuffer = Rhi::CreateBuffer(rhi_buffer_desc{
        .size = kPerFrameUploadMaxSize * Rhi::GetNumFramesInFlight(),
        .bufferUsage = rhi_buffer_usage::CopySrc,
        .memoryUsage = rhi_memory_usage::CpuToGpu,
    });
    Rhi::NameBuffer(g_State->m_StagingBuffer, "StagingBuffer"_s);

    g_State->m_StaticVertexBufferSize = 1_GiB;
    g_State->m_StaticVertexBufferAt = 0;
    g_State->m_StaticVertexBuffer = Rhi::CreateBuffer({
        .size = g_State->m_StaticVertexBufferSize,
        .bufferUsage = rhi_buffer_usage::Vertex | rhi_buffer_usage::CopyDst,
        .memoryUsage = rhi_memory_usage::GpuOnly,
    });
    Rhi::NameBuffer(g_State->m_StaticVertexBuffer, "StaticVertexBuffer"_s);

    g_State->m_StaticIndexBufferSize = 256_MiB;
    g_State->m_StaticIndexBufferAt = 0;
    g_State->m_StaticIndexBuffer = Rhi::CreateBuffer({
        .size = g_State->m_StaticIndexBufferSize,
        .bufferUsage = rhi_buffer_usage::Index | rhi_buffer_usage::CopyDst,
        .memoryUsage = rhi_memory_usage::GpuOnly,
    });
    Rhi::NameBuffer(g_State->m_StaticIndexBuffer, "StaticIndexBuffer"_s);
}

void API Update()
{
    g_State->m_StagingBufferAt = 0;
}

auto API CmdCopyBuffer(rhi_cmdlist cmd, rhi_buffer dst, uint64_t dstOffset, uint64_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    Rhi::CmdCopyBuffer(cmd, dst, dstOffset, g_State->m_StagingBuffer, offset, copySize);
    g_State->m_StagingBufferAt += copySize;

    char *ret = Rhi::MapBuffer(g_State->m_StagingBuffer) + offset;
    return ret;
}

auto API CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, uint64_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    Rhi::CmdCopyTexture(cmd, dst, g_State->m_StagingBuffer, offset, copySize);
    g_State->m_StagingBufferAt += copySize;

    char *ret = Rhi::MapBuffer(g_State->m_StagingBuffer) + offset;
    return ret;
}

auto API CmdCopyStaticVertices(rhi_cmdlist cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *
{
    outBufferOffset = g_State->m_StaticVertexBufferAt;

    char *ret = CmdCopyBuffer(cmd, g_State->m_StaticVertexBuffer, g_State->m_StaticVertexBufferAt, copySize);
    g_State->m_StaticVertexBufferAt += copySize;
    return ret;
}

auto API CmdCopyStaticIndices(rhi_cmdlist cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *
{
    outBufferOffset = g_State->m_StaticIndexBufferAt;

    char *ret = CmdCopyBuffer(cmd, g_State->m_StaticIndexBuffer, g_State->m_StaticIndexBufferAt, copySize);
    g_State->m_StaticIndexBufferAt += copySize;
    return ret;
}

void API CmdBindStaticMeshVertexBuffer(rhi_cmdlist cmd, uint64_t offset)
{
    Rhi::CmdBindVertexBuffers(cmd, 0, {&g_State->m_StaticVertexBuffer, 1}, {&offset, 1});
}

void API CmdBindStaticMeshIndexBuffer(rhi_cmdlist cmd, uint64_t offset)
{
    Rhi::CmdBindIndexBuffer(cmd, g_State->m_StaticIndexBuffer, offset);
}

} // namespace GpuUpload

} // namespace nyla