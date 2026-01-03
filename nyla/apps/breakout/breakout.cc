#include "nyla/apps/breakout/breakout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

#include "nyla/commons/color.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/os/clock.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/renderer2d.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/platform.h"
#include "nyla/platform/platform_key_resolver.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

GameState *g_State;

namespace
{

Renderer2D *renderer2d;
DebugTextRenderer *debugTextRenderer;

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
    g_State = new GameState{};
    g_State->input.moveLeft = g_InputManager->NewId();
    g_State->input.moveRight = g_InputManager->NewId();

    renderer2d = CreateRenderer2D();
    debugTextRenderer = CreateDebugTextRenderer();

    //

    PlatformKeyResolver keyResolver{};
    keyResolver.Init();
    g_InputManager->Map(g_State->input.moveLeft, 1, keyResolver.ResolveKeyCode(KeyPhysical::S));
    g_InputManager->Map(g_State->input.moveRight, 1, keyResolver.ResolveKeyCode(KeyPhysical::F));
    keyResolver.Destroy();

    //

    {
#if defined(__linux__) // TODO: deal with this
        std::string assetsBasePath = "assets/BBreaker";
#else
        std::string assetsBasePath = "D:\\nyla\\assets\\BBreaker";
#endif

        g_State->assets.background = g_AssetManager->DeclareTexture(assetsBasePath + "/Background1.png");
        g_State->assets.player = g_AssetManager->DeclareTexture(assetsBasePath + "/Player.png");
        g_State->assets.playerFlash = g_AssetManager->DeclareTexture(assetsBasePath + "/Player_flash.png");
        g_State->assets.ball = g_AssetManager->DeclareTexture(assetsBasePath + "/Ball_small-blue.png");
        g_State->assets.brickUnbreackable = g_AssetManager->DeclareTexture(assetsBasePath + "/Brick_unbreakable2.png");

        for (uint32_t i = 0; i < g_State->assets.bricks.size(); ++i)
        {
            std::string path = std::format("{}/Brick{}_4.png", assetsBasePath, i + 1);
            g_State->assets.bricks[i] = g_AssetManager->DeclareTexture(path);
        }
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

            g_TweenManager->Lerp(brick.pos[0], x, g_TweenManager->Now(), g_TweenManager->Now() + 1);
            g_TweenManager->Lerp(brick.pos[1], y, g_TweenManager->Now(), g_TweenManager->Now() + 1);
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
    Renderer2DFrameBegin(cmd, renderer2d, g_StagingBuffer);

    const int dx =
        g_InputManager->IsPressed(g_State->input.moveRight) - g_InputManager->IsPressed(g_State->input.moveLeft);

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

void GameRender(RhiCmdList cmd, RhiTexture colorTarget)
{
    RhiTextureInfo colorTargetInfo = g_Rhi->GetTextureInfo(colorTarget);

    g_Rhi->PassBegin({
        .colorTarget = g_Rhi->GetBackbufferTexture(),
        .state = RhiTextureState::ColorTarget,
    });

    const auto &assets = g_State->assets;

    Renderer2DRect(cmd, renderer2d, 0, 0, 100, 70, float4{}, assets.background.index);

    uint32_t i = 0;
    for (Brick &brick : level.bricks)
    {
        i++;
        if (brick.flags & Brick::kFlagDead)
            continue;

        Renderer2DRect(cmd, renderer2d, brick.pos[0], brick.pos[1], brick.size[0], brick.size[1], float4{},
                       assets.bricks[i % assets.bricks.size()].index);
    }

    uint64_t second = GetMonotonicTimeMillis() / 1000;
    if (second % 2)
    {
        Renderer2DRect(cmd, renderer2d, playerPosX, kPlayerPosY, playerWidth, kPlayerHeight, float4{1.f, 1.f, 1.f, 1},
                       assets.playerFlash.index);
    }
    else
    {
        Renderer2DRect(cmd, renderer2d, playerPosX, kPlayerPosY, playerWidth, kPlayerHeight, float4{1.f, 1.f, 1.f, 1},
                       assets.player.index);
    }

    Renderer2DRect(cmd, renderer2d, ballPos[0], ballPos[1], kBallRadius * 2, kBallRadius * 2, float4{1.f, 1.f, 1.f, 1},
                   assets.ball.index);

    Renderer2DDraw(cmd, renderer2d, colorTargetInfo.width, colorTargetInfo.height, 64);
    DebugTextRendererDraw(cmd, debugTextRenderer);

    g_Rhi->PassEnd({
        .colorTarget = g_Rhi->GetBackbufferTexture(),
        .state = RhiTextureState::Present,
    });
}

} // namespace nyla