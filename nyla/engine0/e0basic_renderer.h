#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/rhi/rhi_cmdlist.h"

namespace nyla
{

struct E0BasicRenderer;

auto CreateE0BasicRenderer() -> E0BasicRenderer *;
void E0BasicRendererBegin(RhiCmdList cmd, E0BasicRenderer *renderer, uint32_t width, uint32_t height,
                          float metersOnScreen);
void E0BasicRendererDrawRect(RhiCmdList cmd, E0BasicRenderer *renderer, float x, float y, float width, float height,
                             float4 color);

} // namespace nyla