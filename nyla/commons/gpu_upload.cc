#include "nyla/commons/gpu_upload.h"

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

namespace
{

constexpr uint64_t kPerFrameUploadMaxSize = 16_MiB;

struct gpu_upload_manager_state
{
    rhi_buffer stagingBuffer;
    uint64_t stagingBufferAt;

    uint64_t staticVertexBufferSize;
    uint64_t staticVertexBufferAt;
    rhi_buffer staticVertexBuffer;

    uint64_t staticIndexBufferSize;
    uint64_t staticIndexBufferAt;
    rhi_buffer staticIndexBuffer;
};
gpu_upload_manager_state *manager;

auto PrepareCopySrc(uint64_t copySize) -> uint64_t
{
    manager->stagingBufferAt = AlignedUp(manager->stagingBufferAt, Rhi::GetOptimalBufferCopyOffsetAlignment());
    ASSERT(manager->stagingBufferAt + copySize <= kPerFrameUploadMaxSize);

    Rhi::BufferMarkWritten(manager->stagingBuffer, manager->stagingBufferAt, copySize);

    const uint64_t ret = kPerFrameUploadMaxSize * Rhi::GetFrameIndex() + manager->stagingBufferAt;
    return ret;
}

} // namespace

namespace GpuUpload
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<gpu_upload_manager_state>(RegionAlloc::g_BootstrapAlloc);

    manager->stagingBufferAt = 0;
    manager->stagingBuffer = Rhi::CreateBuffer(rhi_buffer_desc{
        .size = kPerFrameUploadMaxSize * Rhi::GetNumFramesInFlight(),
        .bufferUsage = rhi_buffer_usage::CopySrc,
        .memoryUsage = rhi_memory_usage::CpuToGpu,
    });
    Rhi::NameBuffer(manager->stagingBuffer, "StagingBuffer"_s);

    manager->staticVertexBufferSize = 1_GiB;
    manager->staticVertexBufferAt = 0;
    manager->staticVertexBuffer = Rhi::CreateBuffer({
        .size = manager->staticVertexBufferSize,
        .bufferUsage = rhi_buffer_usage::Vertex | rhi_buffer_usage::CopyDst,
        .memoryUsage = rhi_memory_usage::GpuOnly,
    });
    Rhi::NameBuffer(manager->staticVertexBuffer, "StaticVertexBuffer"_s);

    manager->staticIndexBufferSize = 256_MiB;
    manager->staticIndexBufferAt = 0;
    manager->staticIndexBuffer = Rhi::CreateBuffer({
        .size = manager->staticIndexBufferSize,
        .bufferUsage = rhi_buffer_usage::Index | rhi_buffer_usage::CopyDst,
        .memoryUsage = rhi_memory_usage::GpuOnly,
    });
    Rhi::NameBuffer(manager->staticIndexBuffer, "StaticIndexBuffer"_s);
}

void API Update()
{
    manager->stagingBufferAt = 0;
}

auto API CmdCopyBuffer(rhi_cmdlist cmd, rhi_buffer dst, uint64_t dstOffset, uint64_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    Rhi::CmdCopyBuffer(cmd, dst, dstOffset, manager->stagingBuffer, offset, copySize);
    manager->stagingBufferAt += copySize;

    char *ret = Rhi::MapBuffer(manager->stagingBuffer) + offset;
    return ret;
}

auto API CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, uint64_t copySize) -> char *
{
    uint64_t offset = PrepareCopySrc(copySize);

    Rhi::CmdCopyTexture(cmd, dst, manager->stagingBuffer, offset, copySize);
    manager->stagingBufferAt += copySize;

    char *ret = Rhi::MapBuffer(manager->stagingBuffer) + offset;
    return ret;
}

auto API CmdCopyStaticVertices(rhi_cmdlist cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *
{
    outBufferOffset = manager->staticVertexBufferAt;

    char *ret = CmdCopyBuffer(cmd, manager->staticVertexBuffer, manager->staticVertexBufferAt, copySize);
    manager->staticVertexBufferAt += copySize;
    return ret;
}

auto API CmdCopyStaticIndices(rhi_cmdlist cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *
{
    outBufferOffset = manager->staticIndexBufferAt;

    char *ret = CmdCopyBuffer(cmd, manager->staticIndexBuffer, manager->staticIndexBufferAt, copySize);
    manager->staticIndexBufferAt += copySize;
    return ret;
}

void API CmdBindStaticMeshVertexBuffer(rhi_cmdlist cmd, uint64_t offset)
{
    Rhi::CmdBindVertexBuffers(cmd, 0, {&manager->staticVertexBuffer, 1}, {&offset, 1});
}

void API CmdBindStaticMeshIndexBuffer(rhi_cmdlist cmd, uint64_t offset)
{
    Rhi::CmdBindIndexBuffer(cmd, manager->staticIndexBuffer, offset);
}

} // namespace GpuUpload

} // namespace nyla