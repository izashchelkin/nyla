#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/input_manager.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>
#include <sys/types.h>

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

    struct Paddle
    {
        float2 pos;
        float2 size;
        AssetManager::Texture texture;
    };
};
extern GameState *g_State;

struct Brick
{
    static constexpr uint32_t kFlagDead = 1 << 0;

    uint32_t flags;
    float2 pos;
    float2 size;
    AssetManager::Texture texture;
};

struct GameLevel
{
    InlineVec<Brick, 512> bricks;
};

void GameInit();
void GameProcess(RhiCmdList cmd, float dt);
void GameRender(RhiCmdList cmd, RhiTexture colorTarget);

} // namespace nyla