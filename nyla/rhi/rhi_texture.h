#pragma once

#include <cstdint>

#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_handle.h"

namespace nyla
{

enum class RhiTextureFormat
{
    None,

    // R8_UNorm,
    // R8G8_UNorm,
    // R8G8B8_UNorm,
    // R8G8B8A8_UNorm,
    // B8G8R8A8_UNorm,
    //
    // R8G8B8_sRGB,
    R8G8B8A8_sRGB,
    // B8G8R8A8_sRGB,
    //
    // R16_UNorm,
    // R16G16_UNorm,
    // R16G16B16A16_UNorm,
    //
    // R16_SNorm,
    // R16G16_SNorm,
    // R16G16B16A16_SNorm,
    //
    // R16_UInt,
    // R16G16_UInt,
    // R16G16B16A16_UInt,
    //
    // R16_SInt,
    // R16G16_SInt,
    // R16G16B16A16_SInt,
    //
    // R16_Float,
    // R16G16_Float,
    // R16G16B16A16_Float,
    //
    // R32_UInt,
    // R32G32_UInt,
    // R32G32B32_UInt,
    // R32G32B32A32_UInt,
    //
    // R32_SInt,
    // R32G32_SInt,
    // R32G32B32_SInt,
    // R32G32B32A32_SInt,
    //
    // R32_Float,
    // R32G32_Float,
    // R32G32B32_Float,
    // R32G32B32A32_Float,
    //
    // R11G11B10_Float,
    // R9G9B9E5_SharedExp,
    //
    // D16_UNorm,
    // X8_D24_UNorm,
    // D32_Float,
    //
    // D16_UNorm_S8_UInt,
    // D24_UNorm_S8_UInt,
    // D32_Float_S8X24_UInt,
    //
    // BC1_RGB_UNorm,
    // BC1_RGB_sRGB,
    // BC1_RGBA_UNorm,
    // BC1_RGBA_sRGB,
    //
    // BC2_UNorm,
    // BC2_sRGB,
    //
    // BC3_UNorm,
    // BC3_sRGB,
    //
    // BC4_UNorm,
    // BC4_SNorm,
    //
    // BC5_UNorm,
    // BC5_SNorm,
    //
    // BC6H_UFloat,
    // BC6H_SFloat,
    //
    // BC7_UNorm,
    // BC7_sRGB,
};

struct RhiTexture : RhiHandle
{
};

struct RhiTextureDesc
{
    uint32_t width;
    uint32_t height;
    RhiMemoryUsage memory_usage;
    RhiTextureFormat format;
};

auto RhiCreateTexture(RhiTextureDesc) -> RhiTexture;
void RhiDestroyTexture(RhiTexture);

auto RhiGetBackbufferFormat() -> RhiTextureFormat;

} // namespace nyla