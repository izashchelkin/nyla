#include "nyla/apps/breakout/breakout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "nyla/apps/breakout/unitshapes.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/color.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/math/vec/vec3f.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/fwk/dbg_text_renderer.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/platform/abstract_input.h"
#include "nyla/rhi/rhi.h"

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
    Vec3f color;
    bool dead;
};

struct Level
{
    std ::vector<Brick> bricks;
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
    uint32_t frameIndex = RhiFrameGetIndex();
    if (frameIndex >= vec.size())
    {
        vec.emplace_back(create());
    }
    return vec.at(frameIndex);
}

constexpr Vec2f kWorldBoundaryX{-35.f, 35.f};
constexpr Vec2f kWorldBoundaryY{-30.f, 30.f};
constexpr float kBallRadius = .8f;

static float playerPosX = 0.f;
static const float kPlayerPosY = kWorldBoundaryY[0] + 1.6f;
static float playerWidth = 3.f;
static const float kPlayerHeight = .8f;

static Vec2f ballPos = {};
static Vec2f ballVel = {40.f, 40.f};

static Level level;

void BreakoutInit()
{
    for (size_t i = 0; i < 12; ++i)
    {
        float h = std::fmod(static_cast<float>(i) + 825.f, 12.f) / 12.f;
        float s = .97f;
        float v = .97f;

        Vec3f color = ConvertHsvToRgb(h, s, v);

        for (size_t j = 0; j < 17; ++j)
        {
            level.bricks.emplace_back(Brick{
                -28.f + j * 3.5f,
                20.f - i * 1.5f,
                3.f,
                1.f,
                color,
            });
        }
    }
}

static auto IsInside(float pos, float size, Vec2f boundary) -> bool
{
    if (pos > boundary[0] - size / 2.f && pos < boundary[1] + size / 2.f)
    {
        return true;
    }
    return false;
}

void BreakoutFrame(float dt, uint32_t fps)
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

        Vec2fAdd(ballPos, Vec2fMul(ballVel, kStep));

        for (auto &brick : level.bricks)
        {
            if (brick.dead)
                continue;

            bool hit = false;

            if (IsInside(ballPos[0], kBallRadius * 2.f,
                         Vec2f{brick.x - brick.width / 2.f, brick.x + brick.width / 2.f}))
            {
                if (IsInside(ballPos[1], kBallRadius * 2.f,
                             Vec2f{brick.y - brick.height / 2.f, brick.y + brick.height / 2.f}))
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
                         Vec2f{playerPosX - playerWidth / 2.f, playerPosX + playerWidth / 2.f}))
            {
                if (IsInside(ballPos[1], kBallRadius * 2.f,
                             Vec2f{kPlayerPosY - kPlayerHeight / 2.f, kPlayerPosY + kPlayerHeight / 2.f}))
                {
                    ballVel[1] = -ballVel[1];
                    ballVel[0] = -ballVel[0];
                }
            }
        }
    }

    RpBegin(worldPipeline);
    WorldSetUp();

    {
        static std ::vector<RpMesh> unitRectMeshes;
        RpMesh unitRectMesh = FrameLocal(unitRectMeshes, [] -> RpMesh {
            std::vector<Vertex> unitRect;
            unitRect.reserve(6);
            GenUnitRect([&unitRect](float x, float y) -> void { unitRect.emplace_back(Vertex{Vec2f{x, y}}); });
            return RpVertCopy(worldPipeline, unitRect.size(), CharViewSpan(std::span{unitRect}));
        });

        static std ::vector<RpMesh> unitCircleMeshes;
        RpMesh unitCircleMesh = FrameLocal(unitCircleMeshes, [] -> RpMesh {
            std::vector<Vertex> unitCircle;
            unitCircle.reserve(32 * 3);
            GenUnitCircle(32, [&unitCircle](float x, float y) -> void { unitCircle.emplace_back(Vertex{Vec2f{x, y}}); });
            return RpVertCopy(worldPipeline, unitCircle.size(), CharViewSpan(std::span{unitCircle}));
        });

        if (stage == GameStage::KGame)
        {
            for (Brick &brick : level.bricks)
            {
                if (brick.dead)
                    continue;

                WorldRender(Vec2f{brick.x, brick.y}, brick.color, brick.width, brick.height, unitRectMesh);
            }

            WorldRender(Vec2f{playerPosX, kPlayerPosY}, Vec3f{.1f, .1f, 1.f}, playerWidth, kPlayerHeight,
                        unitRectMesh);

            WorldRender(ballPos, Vec3f{.5f, 0.f, 1.f}, kBallRadius, kBallRadius, unitCircleMesh);
        }
    }

    RpBegin(dbgTextPipeline);
    {
        DbgText(500, 10, absl::StrFormat("fps=%d ball=(%.1f, %.1f)", fps, ballPos[0], ballPos[1]));
    }
}

} // namespace nyla