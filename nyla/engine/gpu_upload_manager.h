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

    auto CmdCopyStaticVertices(RhiCmdList cmd, uint32_t copySize) -> char *
    {
        char *ret = CmdCopyBuffer(cmd, m_StaticVertexBuffer, m_StaticVertexBufferAt, copySize);
        m_StaticVertexBufferAt += copySize;
        return ret;
    }

    auto CmdCopyStaticIndices(RhiCmdList cmd, uint32_t copySize) -> char *
    {
        char *ret = CmdCopyBuffer(cmd, m_StaticIndexBuffer, m_StaticVertexBufferAt, copySize);
        m_StaticIndexBufferAt += copySize;
        return ret;
    }

    auto CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, uint32_t copySize) -> char *;
    auto CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint32_t size) -> char *;

  private:
    auto PrepareCopySrc(uint64_t copySize) -> uint64_t;

    RhiBuffer m_StagingBuffer;
    uint64_t m_StagingBufferAt;

    uint64_t m_StaticVertexBufferSize;
    uint64_t m_StaticVertexBufferAt;
    RhiBuffer m_StaticVertexBuffer;

    uint64_t m_StaticIndexBufferSize;
    uint64_t m_StaticIndexBufferAt;
    RhiBuffer m_StaticIndexBuffer;
};

} // namespace nyla