#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/commons/handle.h"
#include <cstdint>
#include <span>

namespace nyla
{

enum class RhiShaderStage : uint32_t
{
    Vertex = 1 << 0,
    Pixel = 1 << 1,
};

template <> struct EnableBitMaskOps<RhiShaderStage> : std::true_type
{
};

struct RhiShader : Handle
{
};

struct RhiShaderDesc
{
    std::span<const uint32_t> spirv;
};

} // namespace nyla