#include "nyla/apps/breakout/breakout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "nyla/commons/color.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/os/clock.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/renderer2d.h"
#include "nyla/engine/tween_manager.h"
#include "nyla/platform/abstract_input.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

#define X(key) const AbstractInputMapping k##key;
NYLA_INPUT_MAPPING(X)
#undef X

namespace
{

struct Brick
{
    float x;
    float y;
    float width;
    float height;
    float3 color;
    bool dead;
};

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

template <typename T> static auto FrameLocal(std::vector<T> &vec, auto create) -> T &
{
    vec.reserve(RhiGetNumFramesInFlight());
    uint32_t frameIndex = RhiGetFrameIndex();
    if (frameIndex >= vec.size())
    {
        vec.emplace_back(create());
    }
    return vec.at(frameIndex);
}

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

void BreakoutInit(TweenManager *tweenManager)
{
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
                .x = 50.f * (j % 2 ? 1 : -1),
                .y = 50.f * (j % 3 ? -1 : 1),
                .width = 40.f / 15.f,
                .height = 1.f,
                .color = color,
            });

            tweenManager->Lerp(brick.x, x, tweenManager->Now(), tweenManager->Now() + 1);
            tweenManager->Lerp(brick.y, y, tweenManager->Now(), tweenManager->Now() + 1);
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

void BreakoutProcess(float dt, TweenManager *tweenManager)
{
    const int dx = Pressed(kRight) - Pressed(kLeft);

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
            if (brick.dead)
                continue;

            bool hit = false;

            if (IsInside(ballPos[0], kBallRadius * 2.f,
                         float2{brick.x - brick.width / 2.f, brick.x + brick.width / 2.f}))
            {
                if (IsInside(ballPos[1], kBallRadius * 2.f,
                             float2{brick.y - brick.height / 2.f, brick.y + brick.height / 2.f}))
                {
                    ballVel[1] = -ballVel[1];
                    ballVel[0] = -ballVel[0];
                    hit = true;
                    brick.dead = true;
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

void BreakoutRenderGame(RhiCmdList cmd, Renderer2D *renderer, const RhiTextureInfo &colorTargetInfo,
                        const BreakoutAssets &assets)
{
    Renderer2DRect(cmd, renderer, 0, 0, 100, 70, float4{}, assets.background.index);

    uint32_t i = 0;
    for (Brick &brick : level.bricks)
    {
        i++;
        if (brick.dead)
            continue;

        Renderer2DRect(cmd, renderer, brick.x, brick.y, brick.width, brick.height,
                       float4{brick.color[0], brick.color[1], brick.color[2], 1.f},
                       assets.bricks[i % assets.bricks.size()].index);
    }

    uint64_t second = GetMonotonicTimeMillis() / 1000;
    if (second % 2)
    {
        Renderer2DRect(cmd, renderer, playerPosX, kPlayerPosY, playerWidth, kPlayerHeight, float4{1.f, 1.f, 1.f, 1},
                       assets.playerFlash.index);
    }
    else
    {
        Renderer2DRect(cmd, renderer, playerPosX, kPlayerPosY, playerWidth, kPlayerHeight, float4{1.f, 1.f, 1.f, 1},
                       assets.player.index);
    }

    Renderer2DRect(cmd, renderer, ballPos[0], ballPos[1], kBallRadius * 2, kBallRadius * 2, float4{1.f, 1.f, 1.f, 1},
                   assets.ball.index);
}

auto InitBreakoutAssets() -> BreakoutAssets
{
    std::string basePath = "assets/BBreaker";

    BreakoutAssets ret{
        .background = assetManager->DeclareTexture(basePath + "/Background1.png"),
        .player = assetManager->DeclareTexture(basePath + "/Player.png"),
        .playerFlash = assetManager->DeclareTexture(basePath + "/Player_flash.png"),
        .ball = assetManager->DeclareTexture(basePath + "/Ball_small-blue.png"),
        .brickUnbreackable = assetManager->DeclareTexture(basePath + "/Brick_unbreakable2.png"),
    };

    for (uint32_t i = 0; i < ret.bricks.size(); ++i)
    {
        std::string path = std::format("{}/Brick{}_4.png", basePath, i + 1);
        ret.bricks[i] = assetManager->DeclareTexture(path);
    }

    return ret;
}

} // namespace nyla