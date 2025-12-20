#pragma once

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>

namespace nyla
{

struct StagingBuffer;

auto CreateStagingBuffer(uint32_t size) -> StagingBuffer *;
auto AcquireStagingBuffer(RhiCmdList cmd, StagingBuffer *stagingBuffer, RhiBuffer dst, uint32_t dstOffset,
                          uint32_t size) -> char *;
auto ResetStagingBuffer(StagingBuffer *stagingBuffer);

} // namespace nyla