#pragma once

#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct RhiPassDesc
{
    RhiTexture colorTarget;
    RhiTextureState state;
};

} // namespace nyla