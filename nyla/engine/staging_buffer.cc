#include "nyla/engine/staging_buffer.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/memory/align.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

struct GpuStagingBuffer
{
    RhiBuffer buffer;
    uint32_t written;
};

auto CreateStagingBuffer(uint32_t size) -> GpuStagingBuffer *
{
    auto *stagingBuffer = new GpuStagingBuffer{};

    stagingBuffer->buffer = g_Rhi->CreateBuffer(RhiBufferDesc{
        .size = size,
        .bufferUsage = RhiBufferUsage::CopySrc,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });

    return stagingBuffer;
}

namespace
{

auto BeforeCopy(GpuStagingBuffer *stagingBuffer, uint32_t copySize) -> char *
{
    AlignUp(stagingBuffer->written, g_Rhi->GetOptimalBufferCopyOffsetAlignment());

    NYLA_ASSERT(stagingBuffer->written + copySize <= g_Rhi->GetBufferSize(stagingBuffer->buffer));
    char *ret = g_Rhi->MapBuffer(stagingBuffer->buffer) + stagingBuffer->written;

    g_Rhi->BufferMarkWritten(stagingBuffer->buffer, stagingBuffer->written, copySize);
    return ret;
}

} // namespace

auto StagingBufferCopyIntoBuffer(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, RhiBuffer dst, uint32_t dstOffset,
                                 uint32_t size) -> char *
{
    char *ret = BeforeCopy(stagingBuffer, size);

    g_Rhi->CmdCopyBuffer(cmd, dst, dstOffset, stagingBuffer->buffer, stagingBuffer->written, size);

    stagingBuffer->written += size;
    return ret;
}

auto StagingBufferCopyIntoTexture(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, RhiTexture dst, uint32_t size)
    -> char *
{
    char *ret = BeforeCopy(stagingBuffer, size);

    g_Rhi->CmdCopyTexture(cmd, dst, stagingBuffer->buffer, stagingBuffer->written, size);

    stagingBuffer->written += size;
    return ret;
}

void StagingBufferReset(GpuStagingBuffer *stagingBuffer)
{
    stagingBuffer->written = 0;
}

} // namespace nyla