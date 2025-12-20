#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/commons/handle.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

enum class RhiBufferUsage : uint32_t
{
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    TransferSrc = 1 << 3,
    TransferDst = 1 << 4,
};

template <> struct EnableBitMaskOps<RhiBufferUsage> : std::true_type
{
};

enum class RhiMemoryUsage
{
    Unknown = 0,
    GpuOnly,
    CpuToGpu,
    GpuToCpu
};

enum class RhiBufferState
{
    Undefined = 0,
    CopySrc,
    CopyDst,
    ShaderRead,
    ShaderWrite,
    Vertex,
    Index,
    Indirect,
};

struct RhiBuffer : Handle
{
};

struct RhiBufferDesc
{
    uint32_t size;
    RhiBufferUsage bufferUsage;
    RhiMemoryUsage memoryUsage;
};

auto RhiCreateBuffer(const RhiBufferDesc &) -> RhiBuffer;
void RhiNameBuffer(RhiBuffer, std::string_view name);
void RhiDestroyBuffer(RhiBuffer);

auto RhiMapBuffer(RhiBuffer) -> char *;
void RhiUnmapBuffer(RhiBuffer);
void RhiBufferMarkWritten(RhiBuffer, uint32_t offset, uint32_t size);

void RhiCmdCopyBuffer(RhiCmdList cmd, RhiBuffer dst, uint32_t dstOffset, RhiBuffer src, uint32_t srcOffset,
                      uint32_t size);
void RhiCmdTransitionBuffer(RhiCmdList cmd, RhiBuffer buffer, RhiBufferState newState);
void RhiCmdUavBarrierBuffer(RhiCmdList cmd, RhiBuffer buffer);

} // namespace nyla