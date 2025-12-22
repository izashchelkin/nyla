#include "nyla/engine0/staging_buffer.h"
#include "absl/log/check.h"
#include "nyla/commons/memory/align.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

struct StagingBuffer
{
    RhiBuffer buffer;
    uint32_t written;
};

auto CreateStagingBuffer(uint32_t size) -> StagingBuffer *
{
    auto *stagingBuffer = new StagingBuffer{};

    stagingBuffer->buffer = RhiCreateBuffer(RhiBufferDesc{
        .size = size,
        .bufferUsage = RhiBufferUsage::CopySrc,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });

    return stagingBuffer;
}

namespace
{

auto BeforeCopy(StagingBuffer *stagingBuffer, uint32_t copySize) -> char *
{
    AlignUp(stagingBuffer->written, RhiGetOptimalBufferCopyOffsetAlignment());

    CHECK_LE(stagingBuffer->written + copySize, RhiGetBufferSize(stagingBuffer->buffer));
    char *ret = RhiMapBuffer(stagingBuffer->buffer) + stagingBuffer->written;

    RhiBufferMarkWritten(stagingBuffer->buffer, stagingBuffer->written, copySize);
    return ret;
}

} // namespace

auto StagingBufferCopyIntoBuffer(RhiCmdList cmd, StagingBuffer *stagingBuffer, RhiBuffer dst, uint32_t dstOffset,
                                 uint32_t size) -> char *
{
    char *ret = BeforeCopy(stagingBuffer, size);

    RhiCmdCopyBuffer(cmd, dst, dstOffset, stagingBuffer->buffer, stagingBuffer->written, size);

    stagingBuffer->written += size;
    return ret;
}

auto StagingBufferCopyIntoTexture(RhiCmdList cmd, StagingBuffer *stagingBuffer, RhiTexture dst, uint32_t size) -> char *
{
    char *ret = BeforeCopy(stagingBuffer, size);

    RhiCmdCopyTexture(cmd, dst, stagingBuffer->buffer, stagingBuffer->written, size);

    stagingBuffer->written += size;
    return ret;
}

void StagingBufferReset(StagingBuffer *stagingBuffer)
{
    stagingBuffer->written = 0;
}

} // namespace nyla