#pragma once

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

struct GpuStagingBuffer;
extern GpuStagingBuffer *g_StagingBuffer;

auto CreateStagingBuffer(uint32_t size) -> GpuStagingBuffer *;
auto StagingBufferCopyIntoBuffer(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, RhiBuffer dst, uint32_t dstOffset,
                                 uint32_t size) -> char *;
auto StagingBufferCopyIntoTexture(RhiCmdList cmd, GpuStagingBuffer *stagingBuffer, RhiTexture dst, uint32_t size)
    -> char *;
void StagingBufferReset(GpuStagingBuffer *stagingBuffer);

} // namespace nyla