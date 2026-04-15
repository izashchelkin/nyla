#pragma once

#include "nyla/commons/macros.h"

namespace nyla
{

enum class sampler_type
{
    LinearClamp = 0,
    LinearRepeat = 1,
    NearestClamp = 2,
    NearestRepeat = 3,
};

namespace SamplerManager
{

void API Bootstrap();

}

} // namespace nyla
