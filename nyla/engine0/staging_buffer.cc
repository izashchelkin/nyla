#include "nyla/engine0/staging_buffer.h"
#include "nyla/commons/memory/align.h"
#include "nyla/rhi/rhi_buffer.h"
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
        .bufferUsage = RhiBufferUsage::TransferSrc,
        .memoryUsage = RhiMemoryUsage::CpuToGpu,
    });

    return stagingBuffer;
}

auto AcquireStagingBufferChunk(StagingBuffer *stagingBuffer, uint32_t size) -> char *
{
    AlignUp(stagingBuffer->written, 16);
    char *ret = RhiMapBuffer(stagingBuffer->buffer) + stagingBuffer->written;
    stagingBuffer->written += size;
    return ret;
}

} // namespace nyla