#pragma once

#include <cstdint>

#include "nyla/commons/rhi.h"

namespace nyla
{

namespace GpuUpload
{

void API Bootstrap();

void API FrameBegin();

auto API CmdCopyBuffer(rhi_cmdlist cmd, rhi_buffer dst, uint64_t dstOffset, uint64_t copySize) -> char *;
auto API CmdCopyTexture(rhi_cmdlist cmd, rhi_texture dst, uint64_t size) -> char *;

auto API CmdCopyStaticVertices(rhi_cmdlist cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *;
auto API CmdCopyStaticIndices(rhi_cmdlist cmd, uint32_t copySize, uint64_t &outBufferOffset) -> char *;

void API CmdBindStaticMeshVertexBuffer(rhi_cmdlist cmd, uint64_t offset);
void API CmdBindStaticMeshIndexBuffer(rhi_cmdlist cmd, uint64_t offset);

} // namespace GpuUpload

} // namespace nyla