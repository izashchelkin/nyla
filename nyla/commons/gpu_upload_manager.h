#pragma once

#include "nyla/commons/rhi.h"
#include <cstdint>

namespace nyla
{

class GpuUploadManager
{
  public:
    static void Init();

    static void FrameBegin();

    static auto CmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint64_t dstOffset, uint64_t copySize) -> char *;
    static auto CmdCopyTexture(RhiCmdList cmd, RhiTexture dst, uint64_t size) -> char *;

    static auto CmdCopyStaticVertices(RhiCmdList cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *;
    static auto CmdCopyStaticIndices(RhiCmdList cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *;

    static void CmdBindStaticMeshVertexBuffer(RhiCmdList cmd, uint64_t offset);
    static void CmdBindStaticMeshIndexBuffer(RhiCmdList cmd, uint64_t offset);

  private:
};

} // namespace nyla