#include "nyla/apps/shipgame/shipgame.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <numbers>

#include "nyla/apps/shipgame/world_renderer.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/os/clock.h"
#include "nyla/engine0/debug_text_renderer.h"
#include "nyla/platform/abstract_input.h"

namespace nyla
{

#define X(key) const AbstractInputMapping k##key;
NYLA_INPUT_MAPPING(X)
#undef X

namespace
{
using Vertex = Vertex;
}

GameObject gameSolarSystem{};
GameObject gameShip{};

static float2 gameCameraPos = {};
static float gameCameraZoom = 1.f;

static auto GenCircle(size_t n, float radius, float3 color) -> std::vector<Vertex>
{
    NYLA_ASSERT(n > 4);

    std::vector<Vertex> ret;
    ret.reserve(n * 3);

    const float theta = 2.f * std::numbers::pi_v<float> / static_cast<float>(n);
    std::complex<float> r = 1.f;

    for (size_t i = 0; i < n; ++i)
    {
        ret.emplace_back(Vertex{float2{}, color});
        ret.emplace_back(Vertex{float2{r.real() * radius, r.imag() * radius}, color});

        using namespace std::complex_literals;
        r *= std::cos(theta) + std::sin(theta) * 1if;

        ret.emplace_back(Vertex{float2{r.real() * radius, r.imag() * radius}, color});
    }

    return ret;
}

static void RenderGameObject(GameObject &obj)
{
    obj.vertices.clear();

    switch (obj.type)
    {
    case GameObject::Type::KSolarSystem: {
        break;
    }

    case GameObject::Type::KPlanet:
    case GameObject::Type::KMoon: {
        obj.vertices = GenCircle(32, 1.f, obj.color);

        break;
    }

    case GameObject::Type::KShip: {
        obj.vertices.reserve(3);

        obj.vertices.emplace_back(Vertex{{-0.5f, -0.36f}, obj.color});
        obj.vertices.emplace_back(Vertex{{0.5f, 0.0f}, obj.color});
        obj.vertices.emplace_back(Vertex{{-0.5f, 0.36f}, obj.color});

        break;
    }
    }

    if (!obj.vertices.empty())
    {
        WorldRender(obj.pos, obj.angleRadians, obj.scale, obj.vertices);
    }

    for (GameObject &child : obj.children)
    {
        RenderGameObject(child);
    }
}

void ShipgameInit()
{
    {
        gameSolarSystem = {
            .type = GameObject::Type::KSolarSystem,
            .pos = {0.f, 0.f},
            .color = {.1f, .1f, .1f},
            .mass = 100000.f,
            .scale = 5000.f,
            .velocity = {0.f, 0.f},
        };

        auto *planets = new std::vector<GameObject>(3, {.type = GameObject::Type::KPlanet});
        gameSolarSystem.children = {planets->data(), planets->size()};

        size_t iplanet = 0;

        {
            GameObject &planet = gameSolarSystem.children[iplanet++];

            planet.color = {1.f, 0.f, 0.f};

            planet.pos = {100.f, 100.f};
            planet.mass = 100.f;
            planet.scale = 20.f;
            planet.orbitRadius = 4000.f;
        }

        {
            GameObject &planet = gameSolarSystem.children[iplanet++];
            planet.color = {0.f, 1.f, 0.f};

            planet.pos = {-100.f, 100.f};
            planet.mass = 50000.f;
            planet.scale = 10.f;
            planet.orbitRadius = 2000.f;
        }

        {
            GameObject &planet = gameSolarSystem.children[iplanet++];
            planet.color = {0.f, 0.f, 1.f};

            planet.pos = {0, -100.f};
            planet.mass = 50000.f;
            planet.scale = 5.f;
            planet.orbitRadius = 500.f;
        }
    }

    {
        gameShip = {
            .type = GameObject::Type::KShip,
            .pos = {0.f, 0.f},
            .color = {1.f, 1.f, 0.f},
            .mass = 25,
            .scale = 1,
        };
    }
}

void ShipgameFrame(float dt, uint32_t fps)
{
    const int dx = Pressed(kRight) - Pressed(kLeft);
    const int dy = Pressed(kDown) - Pressed(kUp);

    static float dtAccumulator = 0.f;
    dtAccumulator += dt;

    constexpr float kStep = 1.f / 120.f;
    for (; dtAccumulator >= kStep; dtAccumulator -= kStep)
    {
        {
            if (dx || dy)
            {
                float angle = std::atan2(-static_cast<float>(dy), static_cast<float>(dx));
                if (angle < 0.f)
                {
                    angle += 2.f * std::numbers::pi_v<float>;
                }

                gameShip.angleRadians = LerpAngle(gameShip.angleRadians, angle, kStep * 5.f);
            }

            if (Pressed(kBrake))
            {
                AbstractInputRelease(kBoost);

                Lerp(gameShip.velocity, float2{}, kStep * 5.f);
            }
            else if (Pressed(kBoost))
            {
                const float2 direction =
                    float2{std::cos(gameShip.angleRadians), std::sin(gameShip.angleRadians)}.Normalized();

                uint32_t duration = PressedFor(kBoost, GetMonotonicTimeMicros());

                float maxSpeed;
                if (duration < 100000)
                {
                    maxSpeed = 100;
                }
                else
                {
                    maxSpeed = 100 + (duration - 100000) / 10000.f;
                }

                Lerp(gameShip.velocity, direction * maxSpeed, kStep);

                if (gameShip.velocity.Len() < 20)
                    gameShip.velocity = gameShip.velocity * (20.f / gameShip.velocity.Len());
            }
            else
            {
                Lerp(gameShip.velocity, float2{}, kStep);
            }

            gameShip.pos += gameShip.velocity * kStep;
        }

        {
            Lerp(gameCameraPos, gameShip.pos, (gameCameraPos - gameShip.pos).Len() * kStep);
        }

        {
            for (GameObject &planet : gameSolarSystem.children)
            {
                using namespace std::complex_literals;

                const float2 v = gameSolarSystem.pos - planet.pos;
                const float r = v.Len();

                const float2 v2 =
                    (float2(std::complex<float>(planet.pos - gameSolarSystem.pos) * (1.f / 1if)).Normalized() *
                     std::max(0.f, planet.orbitRadius - r / 5.f)) +
                    gameSolarSystem.pos;

                const float2 vv = v2 - planet.pos;

                float f = 6.7f * planet.mass * gameSolarSystem.mass / (r * r);
                float2 fv = vv * (f / vv.Len());

                planet.velocity += fv * (kStep / planet.mass);
                if (planet.velocity.Len() > 10.f)
                    planet.velocity = planet.velocity * (10.f / planet.velocity.Len());

                planet.pos += planet.velocity * kStep;
            }
        }

#if 0
		{
      for (size_t i = 0; i < std::size(entities); ++i) {
        Entity& entity1 = entities[i];
        if (!entity1.exists || !entity1.affected_by_gravity || !entity1.mass)
          continue;

        Vec2f force_sum{};

        float max_f = 0;
        float max_f_orbit_radius = 0;

        for (size_t j = 0; j < std::size(entities); ++j) {
          const Entity& entity2 = entities[j];

          if (j == i) continue;
          if (!entity2.exists || !entity2.affected_by_gravity || !entity2.mass)
            continue;

          using namespace std::complex_literals;

          const Vec2f v = Vec2fDif(entity2.pos, entity1.pos);
          const float r = Vec2fLen(v);

          const Vec2f v2 = Vec2fSum(
              Vec2fMul(Vec2fNorm(Vec2fApply(Vec2fDif(entity1.pos, entity2.pos),
                                            1.f / 1if)),
                       std::max(0.f, entity2.orbit_radius - r / 5.f)),
              entity2.pos);

          const Vec2f vv = Vec2fDif(v2, entity1.pos);

#if 0
            {
              Entity& line = entities[100];
              auto vertices = TriangulateLine(entity1.pos, v2, 10.f);
              memcpy(line.data, vertices.data(),
                     vertices.size() * sizeof(Vertex));
            }
#endif

          // RenderText(
          //     200, 250,
          //     "" + std::to_string(vv[0]) + " " + std::to_string(vv[1]));

          float F = 6.7f * entity1.mass * entity2.mass / (r);
          Vec2f Fv = Vec2fResized(vv, F);

          if (F > max_f) {
            max_f = F;
            max_f_orbit_radius = entity2.orbit_radius;
          }

          Vec2fAdd(force_sum, Fv);
        }

        Vec2fAdd(entity1.velocity, Vec2fMul(force_sum, step / entity1.mass));

        if (max_f) {
          if (Vec2fLen(entity1.velocity) > 1.3 * max_f_orbit_radius) {
            entity1.velocity =
                Vec2fMul(Vec2fNorm(entity1.velocity), 1.3 * max_f_orbit_radius);
          }
        }

        Vec2fAdd(entity1.pos, Vec2fMul(entity1.velocity, step));
      }
    }
#endif
    }

    {
        if (Pressed(kZoomMore))
            gameCameraZoom *= 1.1f;
        if (Pressed(kZoomLess))
            gameCameraZoom /= 1.1f;

        gameCameraZoom = std::clamp(gameCameraZoom, .1f, 2.5f);
    }

    RpBegin(worldPipeline);
    WorldSetUp(gameCameraPos, gameCameraZoom);

    RenderGameObject(gameSolarSystem);
    RenderGameObject(gameShip);

    RpBegin(gridPipeline);
    GridRender();

    RpBegin(dbgTextPipeline);
    DebugText(500, 10, "fps= " + std::to_string(fps));
}

} // namespace nyla