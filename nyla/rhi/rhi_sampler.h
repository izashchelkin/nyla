#pragma once

#include "nyla/rhi/rhi_handle.h"

namespace nyla
{

struct RhiSampler : RhiHandle
{
};

struct RhiSamplerDesc
{
};

auto RhiCreateSampler(RhiSamplerDesc) -> RhiSampler;
void RhiDestroySampler(RhiSampler);

} // namespace nyla