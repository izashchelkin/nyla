#pragma once

#include "nyla/engine0/asset_manager.h"
#include "nyla/engine0/renderer2d.h"
#include "nyla/engine0/tweenmanager.h"
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
auto InitBreakoutAssets(AssetManager *assetManager) -> BreakoutAssets;

void BreakoutInit();
void BreakoutProcess(float dt, TweenManager *tweenmanager);
void BreakoutRenderGame(RhiCmdList cmd, Renderer2D *renderer, const RhiTextureInfo &colorTargetInfo,
                        const BreakoutAssets &assets);

} // namespace nyla