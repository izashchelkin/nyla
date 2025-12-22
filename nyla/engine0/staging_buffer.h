#pragma once

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

struct StagingBuffer;

auto CreateStagingBuffer(uint32_t size) -> StagingBuffer *;
auto StagingBufferCopyIntoBuffer(RhiCmdList cmd, StagingBuffer *stagingBuffer, RhiBuffer dst, uint32_t dstOffset,
                                 uint32_t size) -> char *;
auto StagingBufferCopyIntoTexture(RhiCmdList cmd, StagingBuffer *stagingBuffer, RhiTexture dst, uint32_t size)
    -> char *;
void StagingBufferReset(StagingBuffer *stagingBuffer);

} // namespace nyla