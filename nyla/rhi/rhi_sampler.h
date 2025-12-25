#pragma once

#include "nyla/commons/handle.h"

namespace nyla
{

struct RhiSampler : Handle
{
};

enum class RhiFilter
{
    Linear,
    Nearest,
};

enum class RhiSamplerAddressMode
{
    Repeat,
    ClampToEdge,
};

struct RhiSamplerDesc
{
    RhiFilter minFilter;
    RhiFilter magFilter;
    RhiSamplerAddressMode addressModeU;
    RhiSamplerAddressMode addressModeV;
    RhiSamplerAddressMode addressModeW;
};

auto RhiCreateSampler(const RhiSamplerDesc &) -> RhiSampler;
void RhiDestroySampler(RhiSampler);

} // namespace nyla