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
    kStartMenu,
    kGame,
};
static GameStage stage = GameStage::kGame;

template <typename T> static auto FrameLocal(std::vector<T> &vec, auto create) -> T &
{
    vec.reserve(RhiGetNumFramesInFlight());
    uint32_t frame_index = RhiFrameGetIndex();
    if (frame_index >= vec.size())
    {
        vec.emplace_back(create());
    }
    return vec.at(frame_index);
}

constexpr Vec2f kWorldBoundaryX{-35.f, 35.f};
constexpr Vec2f kWorldBoundaryY{-30.f, 30.f};
constexpr float kBallRadius = .8f;

static float player_pos_x = 0.f;
static const float player_pos_y = kWorldBoundaryY[0] + 1.6f;
static float player_width = 3.f;
static const float player_height = .8f;

static Vec2f ball_pos = {};
static Vec2f ball_vel = {40.f, 40.f};

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

    static float dt_accumulator = 0.f;
    dt_accumulator += dt;

    constexpr float step = 1.f / 120.f;
    for (; dt_accumulator >= step; dt_accumulator -= step)
    {
        player_pos_x += 100.f * step * dx;
        player_pos_x =
            std::clamp(player_pos_x, kWorldBoundaryX[0] + player_width / 2.f, kWorldBoundaryX[1] - player_width / 2.f);

        if (!IsInside(ball_pos[0], kBallRadius * 2.f, kWorldBoundaryX))
        {
            ball_vel[0] = -ball_vel[0];
        }

        if (!IsInside(ball_pos[1], kBallRadius * 2.f, kWorldBoundaryY))
        {
            ball_vel[1] = -ball_vel[1];
        }

        // What's happening?
        // ball_pos[0] = std::clamp(ball_pos[0], kWorldBoundaryX[0] + kBallRadius, kWorldBoundaryX[1] - kBallRadius);
        // ball_pos[1] = std::clamp(ball_pos[1], kWorldBoundaryY[0] + kBallRadius, kWorldBoundaryY[1] - kBallRadius);

        Vec2fAdd(ball_pos, Vec2fMul(ball_vel, step));

        for (auto &brick : level.bricks)
        {
            if (brick.dead)
                continue;

            bool hit = false;

            if (IsInside(ball_pos[0], kBallRadius * 2.f,
                         Vec2f{brick.x - brick.width / 2.f, brick.x + brick.width / 2.f}))
            {
                if (IsInside(ball_pos[1], kBallRadius * 2.f,
                             Vec2f{brick.y - brick.height / 2.f, brick.y + brick.height / 2.f}))
                {
                    ball_vel[1] = -ball_vel[1];
                    ball_vel[0] = -ball_vel[0];
                    hit = true;
                    brick.dead = true;
                }
            }

            if (hit)
                break;
        }

        {
            if (IsInside(ball_pos[0], kBallRadius * 2.f,
                         Vec2f{player_pos_x - player_width / 2.f, player_pos_x + player_width / 2.f}))
            {
                if (IsInside(ball_pos[1], kBallRadius * 2.f,
                             Vec2f{player_pos_y - player_height / 2.f, player_pos_y + player_height / 2.f}))
                {
                    ball_vel[1] = -ball_vel[1];
                    ball_vel[0] = -ball_vel[0];
                }
            }
        }
    }

    RpBegin(worldPipeline);
    WorldSetUp();

    {
        static std ::vector<RpMesh> unit_rect_meshes;
        RpMesh unit_rect_mesh = FrameLocal(unit_rect_meshes, [] -> RpMesh {
            std::vector<Vertex> unit_rect;
            unit_rect.reserve(6);
            GenUnitRect([&unit_rect](float x, float y) -> void { unit_rect.emplace_back(Vertex{Vec2f{x, y}}); });
            return RpVertCopy(worldPipeline, unit_rect.size(), CharViewSpan(std::span{unit_rect}));
        });

        static std ::vector<RpMesh> unit_circle_meshes;
        RpMesh unit_circle_mesh = FrameLocal(unit_circle_meshes, [] -> RpMesh {
            std::vector<Vertex> unit_circle;
            unit_circle.reserve(32 * 3);
            GenUnitCircle(32, [&unit_circle](float x, float y) -> void { unit_circle.emplace_back(Vertex{Vec2f{x, y}}); });
            return RpVertCopy(worldPipeline, unit_circle.size(), CharViewSpan(std::span{unit_circle}));
        });

        if (stage == GameStage::kGame)
        {
            for (Brick &brick : level.bricks)
            {
                if (brick.dead)
                    continue;

                WorldRender(Vec2f{brick.x, brick.y}, brick.color, brick.width, brick.height, unit_rect_mesh);
            }

            WorldRender(Vec2f{player_pos_x, player_pos_y}, Vec3f{.1f, .1f, 1.f}, player_width, player_height,
                        unit_rect_mesh);

            WorldRender(ball_pos, Vec3f{.5f, 0.f, 1.f}, kBallRadius, kBallRadius, unit_circle_mesh);
        }
    }

    RpBegin(dbg_text_pipeline);
    {
        DbgText(500, 10, absl::StrFormat("fps=%d ball=(%.1f, %.1f)", fps, ball_pos[0], ball_pos[1]));
    }
}

} // namespace nyla