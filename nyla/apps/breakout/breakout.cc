#include "nyla/apps/breakout/breakout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

#include "nyla/commons/assert.h"
#include "nyla/commons/color.h"
#include "nyla/commons/math/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/input_manager.h"
#include "nyla/engine/renderer.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

GameState g_State{};

namespace
{

struct Level
{
    InlineVec<Brick, 256> bricks;
};

} // namespace

enum class GameStage
{
    KStartMenu,
    KGame,
};
static GameStage stage = GameStage::KGame;

constexpr float2 kWorldBoundaryX{-35.f, 35.f};
constexpr float2 kWorldBoundaryY{-30.f, 30.f};
constexpr float kBallRadius = .8f;

static float playerPosX = 0.f;
static const float kPlayerPosY = kWorldBoundaryY[0] + 1.6f;
static const float kPlayerHeight = 2.5f;
static float playerWidth = (74.f / 26.f) * kPlayerHeight;

static float2 ballPos = {};
static float2 ballVel = {40.f, 40.f};

static Level level;

void GameInit()
{
    auto &inputManager = g_Engine.GetInputManager();
    auto &tweenManager = g_Engine.GetTweenManager();

    g_State.input.moveLeft = inputManager.NewId();
    inputManager.Map(g_State.input.moveLeft, 1, uint32_t(KeyPhysical::S));

    g_State.input.moveRight = inputManager.NewId();
    inputManager.Map(g_State.input.moveRight, 1, uint32_t(KeyPhysical::F));

    {
#if defined(__linux__) // TODO: deal with this
        std::string assetsBasePath = "assets/BBreaker";
#else
        std::string assetsBasePath = "C:\\nyla\\assets\\BBreaker";
#endif

        auto &assetManager = g_Engine.GetAssetManager();

        g_State.assets.background = assetManager.DeclareTexture(assetsBasePath + "/Background1.png");
        g_State.assets.player = assetManager.DeclareTexture(assetsBasePath + "/Player.png");
        g_State.assets.playerFlash = assetManager.DeclareTexture(assetsBasePath + "/Player_flash.png");
        g_State.assets.ball = assetManager.DeclareTexture(assetsBasePath + "/Ball_small-blue.png");
        g_State.assets.brickUnbreackable = assetManager.DeclareTexture(assetsBasePath + "/Brick_unbreakable2.png");

        for (uint32_t i = 0; i < g_State.assets.bricks.size(); ++i)
        {
            std::string path = std::format("{}/Brick{}_4.png", assetsBasePath, i + 1);
            g_State.assets.bricks[i] = assetManager.DeclareTexture(path);
        }

#ifndef WIN32
        g_State.assets.cube = g_Engine.GetAssetManager().DeclareMesh("/home/izashchelkin/Documents/test.glb");
#else
        g_State.assets.cube = assetManager.DeclareMesh("C:\\Users\\ihorz\\Desktop\\test.glb");
#endif
    }

    //

    for (uint32_t i = 0; i < 12; ++i)
    {
        float h = std::fmod(static_cast<float>(i) + 825.f, 12.f) / 12.f;
        float s = .97f;
        float v = .97f;

        float3 color = ConvertHsvToRgb({h, s, v});

        for (uint32_t j = 0; j < 17; ++j)
        {
            float y = 20.f - i * 1.5f;
            float x = -28.f + j * 3.5f;

            Brick &brick = level.bricks.emplace_back(Brick{
                .pos = {50.f * (j % 2 ? 1 : -1), 50.f * (j % 3 ? -1 : 1)},
                .size = {40.f / 15.f, 1.f},
            });

            tweenManager.Lerp(brick.pos[0], x, tweenManager.Now(), tweenManager.Now() + 1);
            tweenManager.Lerp(brick.pos[1], y, tweenManager.Now(), tweenManager.Now() + 1);
        }
    }
}

static auto IsInside(float pos, float size, float2 boundary) -> bool
{
    if (pos > boundary[0] - size / 2.f && pos < boundary[1] + size / 2.f)
    {
        return true;
    }
    return false;
}

void GameProcess(RhiCmdList cmd, float dt)
{
    auto &inputManager = g_Engine.GetInputManager();

    const int dx = inputManager.IsPressed(g_State.input.moveRight) - inputManager.IsPressed(g_State.input.moveLeft);

    static float dtAccumulator = 0.f;
    dtAccumulator += dt;

    constexpr float kStep = 1.f / 120.f;
    for (; dtAccumulator >= kStep; dtAccumulator -= kStep)
    {
        playerPosX += 100.f * kStep * dx;
        playerPosX =
            std::clamp(playerPosX, kWorldBoundaryX[0] + playerWidth / 2.f, kWorldBoundaryX[1] - playerWidth / 2.f);

        if (!IsInside(ballPos[0], kBallRadius * 2.f, kWorldBoundaryX))
        {
            ballVel[0] = -ballVel[0];
        }

        if (!IsInside(ballPos[1], kBallRadius * 2.f, kWorldBoundaryY))
        {
            ballVel[1] = -ballVel[1];
        }

        // What's happening?
        // ball_pos[0] = std::clamp(ball_pos[0], kWorldBoundaryX[0] + kBallRadius, kWorldBoundaryX[1] - kBallRadius);
        // ball_pos[1] = std::clamp(ball_pos[1], kWorldBoundaryY[0] + kBallRadius, kWorldBoundaryY[1] - kBallRadius);

        ballPos += ballVel * kStep;

        for (auto &brick : level.bricks)
        {
            if (brick.flags & Brick::kFlagDead)
                continue;

            bool hit = false;

            if (IsInside(ballPos[0], kBallRadius * 2.f,
                         float2{brick.pos[0] - brick.size[0] / 2.f, brick.pos[0] + brick.size[0] / 2.f}))
            {
                if (IsInside(ballPos[1], kBallRadius * 2.f,
                             float2{brick.pos[1] - brick.size[1] / 2.f, brick.pos[1] + brick.size[1] / 2.f}))
                {
                    ballVel[1] = -ballVel[1];
                    ballVel[0] = -ballVel[0];
                    hit = true;
                    brick.flags |= Brick::kFlagDead;
                }
            }

            if (hit)
                break;
        }

        {
            if (IsInside(ballPos[0], kBallRadius * 2.f,
                         float2{playerPosX - playerWidth / 2.f, playerPosX + playerWidth / 2.f}))
            {
                if (IsInside(ballPos[1], kBallRadius * 2.f,
                             float2{kPlayerPosY - kPlayerHeight / 2.f, kPlayerPosY + kPlayerHeight / 2.f}))
                {
                    ballVel[1] = -ballVel[1];
                    ballVel[0] = -ballVel[0];
                }
            }
        }
    }
}

void GameRender(RhiCmdList cmd, RhiRenderTargetView rtv)
{
    auto &renderer2d = g_Engine.GetRenderer2D();
    auto &assets = g_State.assets;

    RhiTextureInfo colorTargetInfo = g_Rhi.GetTextureInfo(g_Rhi.GetTexture(rtv));

    g_Rhi.PassBegin({
        .renderTarget = rtv,
        .state = RhiTextureState::ColorTarget,
    });

    {
        renderer2d.Rect({0, 0}, {100, 70}, assets.background);

        uint32_t i = 0;
        for (Brick &brick : level.bricks)
        {
            i++;
            if (brick.flags & Brick::kFlagDead)
                continue;

            renderer2d.Rect(brick.pos, brick.size, assets.bricks[i % assets.bricks.size()]);
        }

        uint64_t second = g_Platform.GetMonotonicTimeMillis() / 1000;
        if (second % 2)
        {
            renderer2d.Rect({playerPosX, kPlayerPosY}, {playerWidth, kPlayerHeight}, assets.playerFlash);
        }
        else
        {
            renderer2d.Rect({playerPosX, kPlayerPosY}, {playerWidth, kPlayerHeight}, assets.player);
        }

        renderer2d.Rect(ballPos, {kBallRadius * 2, kBallRadius * 2}, assets.ball);
    }

    renderer2d.CmdFlush(cmd, colorTargetInfo.width, colorTargetInfo.height, 64);

    g_Engine.GetDebugTextRenderer().CmdFlush(cmd);

    g_Rhi.PassEnd({
        .renderTarget = rtv,
        .state = RhiTextureState::Present,
    });
}

} // namespace nyla