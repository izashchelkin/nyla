#pragma once

#include <cstdint>

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_handle.h"

namespace nyla
{

enum class RhiTextureFormat
{
    None,

    R8G8B8A8_sRGB,

    D32_Float,
    D32_Float_S8_UINT,
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

auto RhiCreateTexture(RhiTextureDesc) -> RhiTexture;
void RhiDestroyTexture(RhiTexture);

auto RhiGetBackbufferFormat() -> RhiTextureFormat;

} // namespace nyla