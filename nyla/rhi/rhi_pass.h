#pragma once

#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct RhiPassDesc
{
    RhiRenderTargetView rtv;
    RhiTextureState rtState;

    RhiRenderTargetView dsv;
    RhiTextureState dsState;
};

} // namespace nyla