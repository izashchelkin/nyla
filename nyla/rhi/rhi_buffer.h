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
    CopySrc = 1 << 3,
    CopyDst = 1 << 4,
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

} // namespace nyla