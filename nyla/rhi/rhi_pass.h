#pragma once

#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct RhiPassDesc
{
    RhiRenderTargetView renderTarget;
    RhiTextureState state;
};

} // namespace nyla