#include "nyla/commons/staging_buffer.h"
#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/rhi.h"
#include <cstdint>

namespace nyla
{

auto CreateStagingBuffer(uint32_t size) -> GpuStagingBuffer *
{
    auto *stagingBuffer = new GpuStagingBuffer{};

    stagingBuffer->buffer = Rhi::CreateBuffer(RhiBufferDesc{
        .size = size,
        .bufferUsage = RhiBufferUsage::CopySrc,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });

    return stagingBuffer;
}

auto StagingBufferCopyIntoBuffer(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, RhiBuffer dst, uint32_t dstOffset,
                                 uint32_t size) -> char *
{
    char *ret = BeforeCopy(stagingBuffer, size);

    Rhi::CmdCopyBuffer(cmd, dst, dstOffset, stagingBuffer->buffer, stagingBuffer->written, size);

    stagingBuffer->written += size;
    return ret;
}

auto StagingBufferCopyIntoTexture(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, RhiTexture dst, uint32_t size)
    -> char *
{
    char *ret = BeforeCopy(stagingBuffer, size);

    Rhi::CmdCopyTexture(cmd, dst, stagingBuffer->buffer, stagingBuffer->written, size);

    stagingBuffer->written += size;
    return ret;
}

void StagingBufferReset(GpuStagingBuffer *stagingBuffer)
{
    stagingBuffer->written = 0;
}

} // namespace nyla