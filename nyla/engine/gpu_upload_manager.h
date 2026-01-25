#pragma once

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

class GpuUploadManager
{
  public:
    void Init();

    void FrameBegin();

    auto CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, uint32_t copySize) -> char *;
    auto CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint32_t size) -> char *;

  private:
    auto PrepareCopySrc(uint64_t copySize) -> uint64_t;

    RhiBuffer m_StagingBuffer;
    uint64_t m_StagingBufferWritten;
};

} // namespace nyla