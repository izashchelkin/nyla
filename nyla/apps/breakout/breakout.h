#pragma once

#include "nyla/engine0/renderer2d.h"
#include "nyla/platform/abstract_input.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

#define NYLA_INPUT_MAPPING(X) X(Right) X(Left) X(Boost) X(Fire)

#define X(key) extern const AbstractInputMapping k##key;
NYLA_INPUT_MAPPING(X)
#undef X

void BreakoutInit();
void BreakoutProcess(float dt);
void BreakoutRenderGame(RhiCmdList cmd, Renderer2D *renderer, const RhiTextureInfo &colorTargetInfo);

} // namespace nyla