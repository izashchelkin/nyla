#pragma once

#include "nyla/engine/asset_manager.h"
#include "nyla/platform/abstract_input.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

#define NYLA_INPUT_MAPPING(X) X(Right) X(Left) X(Boost) X(Fire)

#define X(key) extern const AbstractInputMapping k##key;
NYLA_INPUT_MAPPING(X)
#undef X

struct BreakoutAssets
{
    AssetManager::Texture background;
    AssetManager::Texture player;
    AssetManager::Texture playerFlash;
    AssetManager::Texture ball;
    AssetManager::Texture brickUnbreackable;
    std::array<AssetManager::Texture, 9> bricks;
};
extern BreakoutAssets assets;

void BreakoutInit();
void BreakoutProcess(RhiCmdList cmd, float dt);
void BreakoutRenderGame(RhiCmdList cmd, RhiTexture colorTarget);

} // namespace nyla