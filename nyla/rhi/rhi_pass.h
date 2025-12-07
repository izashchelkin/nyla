#pragma once

#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct RhiPassDesc
{
    RhiTexture colorTarget;
};

void RhiPassBegin(RhiPassDesc);
void RhiPassEnd(RhiPassDesc);

} // namespace nyla