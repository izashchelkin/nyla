#pragma once

#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct RhiPassDesc
{
    RhiTextureView colorTarget;
    RhiTextureState state;
};

} // namespace nyla