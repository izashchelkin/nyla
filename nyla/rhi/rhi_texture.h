#pragma once

#include <cstdint>

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_handle.h"

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

struct RhiTexture : RhiHandle
{
};

struct RhiTextureDesc
{
    uint32_t width;
    uint32_t height;
    RhiMemoryUsage memoryUsage;
    RhiTextureFormat format;
};

struct RhiTextureInfo
{
    uint32_t width;
    uint32_t height;
    RhiTextureFormat format;
};

auto RhiCreateTexture(RhiTextureDesc) -> RhiTexture;
void RhiDestroyTexture(RhiTexture);
auto RhiGetTextureInfo(RhiTexture) -> RhiTextureInfo;
void RhiCmdTransitionTexture(RhiCmdList, RhiTexture, RhiTextureState);

auto RhiGetBackbufferTexture() -> RhiTexture;

} // namespace nyla