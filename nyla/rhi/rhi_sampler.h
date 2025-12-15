#pragma once

#include "nyla/commons/handle.h"

namespace nyla
{

struct RhiSampler : Handle
{
};

struct RhiSamplerDesc
{
};

auto RhiCreateSampler(RhiSamplerDesc) -> RhiSampler;
void RhiDestroySampler(RhiSampler);

} // namespace nyla