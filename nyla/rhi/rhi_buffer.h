#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/commons/handle.h"
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
    Unknown,
    GpuOnly,
    CpuToGpu,
    GpuToCpu
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

auto RhiMapBuffer(RhiBuffer, bool persistent = true) -> void *;
void RhiUnmapBuffer(RhiBuffer, bool persistent = true);

} // namespace nyla