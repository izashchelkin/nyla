#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/handle.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"

namespace nyla
{

enum class RhiTextureFormat
{
    None,

    R8G8B8A8_sRGB,
    B8G8R8A8_sRGB,

    D32_Float,
    D32_Float_S8_UINT,
};

enum class RhiTextureUsage
{
    None = 0,

    ShaderSampled = 1 << 0,
    ColorTarget = 1 << 1,
    DepthStencil = 1 << 2,
    TransferSrc = 1 << 3,
    TransferDst = 1 << 4,
    Storage = 1 << 5,
    Present = 1 << 6,
};
NYLA_BITENUM(RhiTextureUsage);

enum class RhiTextureState
{
    Undefined,
    ColorTarget,
    DepthTarget,
    ShaderRead,
    Present,
    TransferSrc,
    TransferDst,
};

struct RhiTexture : Handle
{
};

struct RhiTextureView : Handle
{
};

struct RhiTextureDesc
{
    uint32_t width;
    uint32_t height;
    RhiMemoryUsage memoryUsage;
    RhiTextureUsage usage;
    RhiTextureFormat format;
};

struct RhiTextureViewDesc
{
    RhiTexture texture;
    RhiTextureFormat format;
};

struct RhiTextureInfo
{
    uint32_t width;
    uint32_t height;
    RhiTextureFormat format;
};

} // namespace nyla