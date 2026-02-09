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

    auto CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint64_t dstOffset, uint64_t copySize) -> char *;
    auto CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint64_t size) -> char *;

    auto CmdCopyStaticVertices(RhiCmdList cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *;
    auto CmdCopyStaticIndices(RhiCmdList cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *;

    void CmdBindStaticMeshVertexBuffer(RhiCmdList cmd, uint64_t offset);
    void CmdBindStaticMeshIndexBuffer(RhiCmdList cmd, uint64_t offset);

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