#pragma once

#include "nyla/engine/asset_manager.h"
#include "nyla/engine/input_manager.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

struct GameState
{
    struct Input
    {
        InputId moveLeft;
        InputId moveRight;
    };
    Input input;

    struct Assets
    {
        AssetManager::Texture background;
        AssetManager::Texture player;
        AssetManager::Texture playerFlash;
        AssetManager::Texture ball;
        AssetManager::Texture brickUnbreackable;
        std::array<AssetManager::Texture, 9> bricks;
    };
    Assets assets;
};
extern GameState *g_State;

void BreakoutInit();
void BreakoutProcess(RhiCmdList cmd, float dt);
void BreakoutRenderGame(RhiCmdList cmd, RhiTexture colorTarget);

} // namespace nyla