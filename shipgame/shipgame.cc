#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

#include "assets.h"
#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/inline_vec_def.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/lerp.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

constexpr uint32_t kMaxPlanets = 8;
constexpr uint32_t kMaxVerticesPerFrame = 1 << 14;

struct vertex
{
    float2 pos;
    array<float, 2> pad0;
    float3 color;
    array<float, 1> pad1;
};
static_assert(sizeof(vertex) == 32);

struct scene_ubo
{
    float4x4 vp;
    float4x4 invVp;
};

struct entity_ubo
{
    float4x4 model;
};

enum class object_type : uint8_t
{
    SolarSystem,
    Planet,
    Ship,
};

struct game_object
{
    object_type type;
    float2 pos;
    float3 color;
    float angleRadians;
    float mass;
    float scale;
    float orbitRadius;
    float2 velocity;
};

struct game_state
{
    uint64_t targetFrameDurationUs;
    uint64_t lastFrameStartUs;
    uint32_t dtUsAccum;
    uint32_t framesCounted;
    uint32_t fps;
    bool shouldExit;

    rhi_graphics_pipeline worldPipeline;
    rhi_graphics_pipeline gridPipeline;
    rhi_buffer vertexBuffer;

    game_object solarSystem;
    inline_vec<game_object, kMaxPlanets> planets;
    game_object ship;

    float2 cameraPos;
    float cameraZoom;

    uint64_t boostStartUs;
    bool boostActive;
};
game_state *game;

auto MakeOrtho(uint32_t width, uint32_t height, float2 cameraPos, float zoom) -> scene_ubo
{
    constexpr float kMetersOnScreen = 64.f;

    float worldW;
    float worldH;

    const float base = kMetersOnScreen * zoom;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (aspect >= 1.f)
    {
        worldH = base;
        worldW = base * aspect;
    }
    else
    {
        worldW = base;
        worldH = base / aspect;
    }

    float4x4 view = Mat::Translate(float4{-cameraPos[0], -cameraPos[1], 0.f, 1.f});
    float4x4 proj = Mat::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);

    float4x4 vp = proj * view;
    float4x4 invVp = Mat::Inverse(vp);

    return scene_ubo{
        .vp = vp,
        .invVp = invVp,
    };
}

auto BuildModel(float2 pos, float angleRadians, float scalar) -> float4x4
{
    float4x4 model = Mat::Translate(float4{pos[0], pos[1], 0.f, 1.f});
    model = model * Mat::Rotate(angleRadians);
    model = model * Mat::Scale(float4{scalar, scalar, 1.f, 1.f});
    return model;
}

auto WriteCircleVertices(span<vertex> dst, uint32_t segments, float radius, float3 color) -> uint32_t
{
    ASSERT(segments >= 5);
    uint32_t count = segments * 3;
    ASSERT(dst.size >= count);

    const float theta = 2.f * std::numbers::pi_v<float> / static_cast<float>(segments);
    std::complex<float> r = 1.f;

    uint32_t w = 0;
    for (uint32_t i = 0; i < segments; ++i)
    {
        dst[w++] = vertex{.pos = {0.f, 0.f}, .color = color};
        dst[w++] = vertex{.pos = {r.real() * radius, r.imag() * radius}, .color = color};

        using namespace std::complex_literals;
        r *= std::cos(theta) + std::sin(theta) * 1if;

        dst[w++] = vertex{.pos = {r.real() * radius, r.imag() * radius}, .color = color};
    }
    return count;
}

auto WriteShipVertices(span<vertex> dst, float3 color) -> uint32_t
{
    ASSERT(dst.size >= 3);
    dst[0] = vertex{.pos = {-0.5f, -0.36f}, .color = color};
    dst[1] = vertex{.pos = {0.5f, 0.f}, .color = color};
    dst[2] = vertex{.pos = {-0.5f, 0.36f}, .color = color};
    return 3;
}

void DrawObject(rhi_cmdlist cmd, const game_object &obj, vertex *vbBase, uint32_t vbCapacity, uint32_t &vbWriteOffset)
{
    uint32_t baseVertex = vbWriteOffset;
    uint32_t vertexCount = 0;

    span<vertex> dst{vbBase + vbWriteOffset, vbCapacity - vbWriteOffset};

    switch (obj.type)
    {
    case object_type::SolarSystem:
        return;
    case object_type::Planet:
        vertexCount = WriteCircleVertices(dst, 32, 1.f, obj.color);
        break;
    case object_type::Ship:
        vertexCount = WriteShipVertices(dst, obj.color);
        break;
    }

    if (!vertexCount)
        return;

    vbWriteOffset += vertexCount;

    entity_ubo entity{
        .model = BuildModel(obj.pos, obj.angleRadians, obj.scale),
    };
    Rhi::SetDrawConstant(cmd, Span::ByteViewPtr(&entity));
    Rhi::CmdDraw(cmd, vertexCount, 1, baseVertex, 0);
}

} // namespace

void UserMain()
{
    game = &RegionAlloc::Alloc<game_state>(RegionAlloc::g_BootstrapAlloc);
    game->targetFrameDurationUs = 1'000'000 / 144;
    game->cameraZoom = 1.f;

    region_alloc alloc = RegionAlloc::Create(16_MiB, 0);

    WinOpen();
    Rhi::Bootstrap(alloc, {
                              .flags = rhi_flags::VSync,
                          });

    InputManager::Bootstrap();
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    TweenManager::Bootstrap();
    AssetManager::Bootstrap(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));
    DebugTextRenderer::Bootstrap(alloc);

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    {
        rhi_shader worldVs = GetShader(ID_shipgame_world_vs, rhi_shader_stage::Vertex);
        rhi_shader worldPs = GetShader(ID_shipgame_world_ps, rhi_shader_stage::Pixel);

        array<rhi_vertex_attribute_desc, 2> worldAttrs{
            rhi_vertex_attribute_desc{
                .binding = 0,
                .semantic = "POSITION0"_s,
                .format = rhi_vertex_format::R32G32B32A32Float,
                .offset = 0,
            },
            rhi_vertex_attribute_desc{
                .binding = 0,
                .semantic = "COLOR0"_s,
                .format = rhi_vertex_format::R32G32B32A32Float,
                .offset = 16,
            },
        };

        rhi_vertex_binding_desc worldBinding{
            .binding = 0,
            .stride = sizeof(vertex),
            .inputRate = rhi_input_rate::PerVertex,
        };

        rhi_texture_format colorFormat = rhi_texture_format::B8G8R8A8_sRGB;

        const rhi_graphics_pipeline_desc worldDesc{
            .debugName = "ShipgameWorld"_s,
            .vs = worldVs,
            .ps = worldPs,
            .vertexBindings = {&worldBinding, 1},
            .vertexAttributes = worldAttrs,
            .colorTargetFormats = {&colorFormat, 1},
            .depthFormat = rhi_texture_format::D32_Float_S8_UINT,
            .cullMode = rhi_cull_mode::None,
            .frontFace = rhi_front_face::CCW,
        };
        game->worldPipeline = Rhi::CreateGraphicsPipeline(alloc, worldDesc);

        rhi_shader gridVs = GetShader(ID_shipgame_grid_vs, rhi_shader_stage::Vertex);
        rhi_shader gridPs = GetShader(ID_shipgame_grid_ps, rhi_shader_stage::Pixel);

        const rhi_graphics_pipeline_desc gridDesc{
            .debugName = "ShipgameGrid"_s,
            .vs = gridVs,
            .ps = gridPs,
            .colorTargetFormats = {&colorFormat, 1},
            .depthFormat = rhi_texture_format::D32_Float_S8_UINT,
            .cullMode = rhi_cull_mode::None,
            .frontFace = rhi_front_face::CCW,
        };
        game->gridPipeline = Rhi::CreateGraphicsPipeline(alloc, gridDesc);
    }

    {
        const uint64_t vertexBufferSize =
            uint64_t{Rhi::GetNumFramesInFlight()} * uint64_t{kMaxVerticesPerFrame} * sizeof(vertex);
        game->vertexBuffer = Rhi::CreateBuffer(rhi_buffer_desc{
            .size = vertexBufferSize,
            .bufferUsage = rhi_buffer_usage::Vertex,
            .memoryUsage = rhi_memory_usage::CpuToGpu,
        });
        Rhi::NameBuffer(game->vertexBuffer, "ShipgameVertices"_s);
    }

    { // game init

        game->lastFrameStartUs = GetMonotonicTimeMicros();

        game->solarSystem = game_object{
            .type = object_type::SolarSystem,
            .pos = {0.f, 0.f},
            .color = {.1f, .1f, .1f},
            .mass = 100000.f,
            .scale = 5000.f,
        };

        InlineVec::Append(game->planets, game_object{
                                             .type = object_type::Planet,
                                             .pos = {100.f, 100.f},
                                             .color = {1.f, 0.f, 0.f},
                                             .mass = 100.f,
                                             .scale = 20.f,
                                             .orbitRadius = 4000.f,
                                         });
        InlineVec::Append(game->planets, game_object{
                                             .type = object_type::Planet,
                                             .pos = {-100.f, 100.f},
                                             .color = {0.f, 1.f, 0.f},
                                             .mass = 50000.f,
                                             .scale = 10.f,
                                             .orbitRadius = 2000.f,
                                         });
        InlineVec::Append(game->planets, game_object{
                                             .type = object_type::Planet,
                                             .pos = {0.f, -100.f},
                                             .color = {0.f, 0.f, 1.f},
                                             .mass = 50000.f,
                                             .scale = 5.f,
                                             .orbitRadius = 500.f,
                                         });

        game->ship = game_object{
            .type = object_type::Ship,
            .pos = {0.f, 0.f},
            .color = {1.f, 1.f, 0.f},
            .mass = 25.f,
            .scale = 1.f,
        };

        InputManager::Map(input_id::MoveForward, input_interface_type::Keyboard, (uint32_t)KeyPhysical::E);
        InputManager::Map(input_id::MoveBackward, input_interface_type::Keyboard, (uint32_t)KeyPhysical::D);
        InputManager::Map(input_id::MoveLeft, input_interface_type::Keyboard, (uint32_t)KeyPhysical::S);
        InputManager::Map(input_id::MoveRight, input_interface_type::Keyboard, (uint32_t)KeyPhysical::F);
        InputManager::Map(input_id::AttackPrimary, input_interface_type::Keyboard, (uint32_t)KeyPhysical::J);
        InputManager::Map(input_id::Sprint, input_interface_type::Keyboard, (uint32_t)KeyPhysical::K);
        InputManager::Map(input_id::Brake, input_interface_type::Keyboard, (uint32_t)KeyPhysical::Space);
        InputManager::Map(input_id::CameraZoomIn, input_interface_type::Keyboard, (uint32_t)KeyPhysical::U);
        InputManager::Map(input_id::CameraZoomOut, input_interface_type::Keyboard, (uint32_t)KeyPhysical::I);
    }

    for (;;)
    {
        RegionAlloc::Reset(alloc);

        rhi_cmdlist cmd = Rhi::FrameBegin(alloc);

        const uint64_t frameStart = GetMonotonicTimeMicros();

        const uint64_t dtUs = frameStart - game->lastFrameStartUs;
        game->dtUsAccum += dtUs;
        ++game->framesCounted;

        const float dt = static_cast<float>(dtUs) * 1e-6f;
        game->lastFrameStartUs = frameStart;

        if (game->dtUsAccum >= (uint64_t)500'000)
        {
            const double seconds = static_cast<double>(game->dtUsAccum) / 1'000'000.0;
            const double fpsF64 = game->framesCounted / seconds;

            game->fps = LRound(fpsF64);
            game->dtUsAccum = 0;
            game->framesCounted = 0;
        }

        DebugTextRenderer::Fmt(500, 10, "fps=%d"_s, uint32_t(game->fps));

        for (;;)
        {
            PlatformEvent event{};
            if (!WinPollEvent(event))
                break;

            switch (event.type)
            {

            case PlatformEventType::KeyDown: {
                InputManager::HandlePressed(input_interface_type::Keyboard, uint32_t(event.key), frameStart);
                break;
            }
            case PlatformEventType::KeyUp: {
                InputManager::HandleReleased(input_interface_type::Keyboard, uint32_t(event.key), frameStart);
                break;
            }

            case PlatformEventType::MousePress: {
                InputManager::HandlePressed(input_interface_type::Mouse, event.mouse.code, frameStart);
                break;
            }
            case PlatformEventType::MouseRelease: {
                InputManager::HandleReleased(input_interface_type::Mouse, event.mouse.code, frameStart);
                break;
            }

            case PlatformEventType::WinResize: {
                Rhi::TriggerSwapchainRecreate();
                break;
            }

            case PlatformEventType::Quit: {
                Exit(0);
            }

            case PlatformEventType::Repaint:
            case PlatformEventType::None:
                break;

            default: {
                TRAP();
                break;
            }
            }
        }

        GpuUpload::Update();
        InputManager::Update();
        TweenManager::Update(dt);
        TextureManager::Update(cmd);

        {
            rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
            rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

            rhi_rtv rtv;
            rhi_dsv dsv;
            RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, &rtv, &dsv);

            {
                // game input
                const int dx =
                    InputManager::IsPressed(input_id::MoveRight) - InputManager::IsPressed(input_id::MoveLeft);
                const int dy =
                    InputManager::IsPressed(input_id::MoveBackward) - InputManager::IsPressed(input_id::MoveForward);
                const bool brake = InputManager::IsPressed(input_id::Brake);
                const bool boost = InputManager::IsPressed(input_id::Sprint) && !brake;

                if (boost && !game->boostActive)
                {
                    game->boostStartUs = frameStart;
                    game->boostActive = true;
                }
                else if (!boost)
                {
                    game->boostActive = false;
                }

                // game simulation
                static uint64_t dtUsAccumulator = 0;
                dtUsAccumulator += dtUs;

                constexpr uint64_t kStepUs = 1'000'000 / 120;
                constexpr float kStep = 1.f / 120.f;
                for (; dtUsAccumulator >= kStepUs; dtUsAccumulator -= kStepUs)
                {
                    if (dx || dy)
                    {
                        float angle = std::atan2(-static_cast<float>(dy), static_cast<float>(dx));
                        if (angle < 0.f)
                            angle += 2.f * std::numbers::pi_v<float>;

                        game->ship.angleRadians = LerpAngle(game->ship.angleRadians, angle, kStep * 5.f);
                    }

                    if (brake)
                    {
                        game->ship.velocity = Lerp(game->ship.velocity, float2{}, kStep * 5.f);
                    }
                    else if (boost)
                    {
                        const float2 direction = Vec::Normalized(
                            float2{std::cos(game->ship.angleRadians), std::sin(game->ship.angleRadians)});

                        const uint64_t duration = frameStart - game->boostStartUs;
                        const float maxSpeed =
                            duration < 100'000 ? 100.f : 100.f + static_cast<float>(duration - 100'000) / 10000.f;

                        game->ship.velocity = Lerp(game->ship.velocity, direction * maxSpeed, kStep);

                        const float speed = Vec::Len(game->ship.velocity);
                        if (speed > 0.f && speed < 20.f)
                            game->ship.velocity = game->ship.velocity * (20.f / speed);
                    }
                    else
                    {
                        game->ship.velocity = Lerp(game->ship.velocity, float2{}, kStep);
                    }

                    game->ship.pos += game->ship.velocity * kStep;

                    const float2 toShip = game->ship.pos - game->cameraPos;
                    game->cameraPos = Lerp(game->cameraPos, game->ship.pos, Vec::Len(toShip) * kStep);

                    for (game_object &planet : game->planets)
                    {
                        using namespace std::complex_literals;

                        const float2 v = game->solarSystem.pos - planet.pos;
                        const float r = Vec::Len(v);
                        if (r <= 1e-3f)
                            continue;

                        const std::complex<float> fromCenter(planet.pos[0] - game->solarSystem.pos[0],
                                                             planet.pos[1] - game->solarSystem.pos[1]);
                        const std::complex<float> rotated = fromCenter * (1.f / 1if);
                        const float2 rotatedF{rotated.real(), rotated.imag()};
                        const float rotatedLen = Vec::Len(rotatedF);
                        if (rotatedLen <= 1e-3f)
                            continue;

                        const float2 v2 = (rotatedF * (1.f / rotatedLen)) * Max(0.f, planet.orbitRadius - r / 5.f) +
                                          game->solarSystem.pos;

                        const float2 vv = v2 - planet.pos;
                        const float vvLen = Vec::Len(vv);
                        if (vvLen <= 1e-3f)
                            continue;

                        const float f = 6.7f * planet.mass * game->solarSystem.mass / (r * r);
                        const float2 fv = vv * (f / vvLen);

                        planet.velocity += fv * (kStep / planet.mass);
                        const float velLen = Vec::Len(planet.velocity);
                        if (velLen > 10.f)
                            planet.velocity = planet.velocity * (10.f / velLen);

                        planet.pos += planet.velocity * kStep;
                    }
                }

                if (InputManager::IsPressed(input_id::CameraZoomIn))
                    game->cameraZoom *= 1.f + 1.5f * dt;
                if (InputManager::IsPressed(input_id::CameraZoomOut))
                    game->cameraZoom /= 1.f + 1.5f * dt;
                game->cameraZoom = Clamp(game->cameraZoom, .1f, 2.5f);

                rhi_texture renderTarget = Rhi::GetTexture(rtv);
                rhi_texture_info rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(cmd, renderTarget, rhi_texture_state::ColorTarget);
                Rhi::CmdTransitionTexture(cmd, Rhi::GetTexture(dsv), rhi_texture_state::DepthTarget);

                Rhi::PassBegin({
                    .rtv = rtv,
                    .dsv = dsv,
                });
                {
                    scene_ubo scene = MakeOrtho(rtInfo.width, rtInfo.height, game->cameraPos, game->cameraZoom);
                    Rhi::SetPassConstant(cmd, Span::ByteViewPtr(&scene));

                    const uint32_t frameIndex = Rhi::GetFrameIndex();
                    const uint64_t vbFrameOffset =
                        uint64_t{frameIndex} * uint64_t{kMaxVerticesPerFrame} * sizeof(vertex);

                    char *vbMappedBase = Rhi::MapBuffer(game->vertexBuffer);
                    vertex *vbBase = reinterpret_cast<vertex *>(vbMappedBase + vbFrameOffset);
                    uint32_t vbWriteOffset = 0;

                    Rhi::CmdBindGraphicsPipeline(cmd, game->worldPipeline);
                    Rhi::CmdBindVertexBuffers(cmd, 0, {&game->vertexBuffer, 1}, {&vbFrameOffset, 1});

                    for (game_object &planet : game->planets)
                        DrawObject(cmd, planet, vbBase, kMaxVerticesPerFrame, vbWriteOffset);
                    DrawObject(cmd, game->ship, vbBase, kMaxVerticesPerFrame, vbWriteOffset);

                    Rhi::BufferMarkWritten(game->vertexBuffer, static_cast<uint32_t>(vbFrameOffset),
                                           vbWriteOffset * sizeof(vertex));

                    {
                        Rhi::CmdBindGraphicsPipeline(cmd, game->gridPipeline);
                        entity_ubo unused{};
                        Rhi::SetDrawConstant(cmd, Span::ByteViewPtr(&unused));
                        Rhi::CmdDraw(cmd, 3, 1, 0, 0);
                    }

                    DebugTextRenderer::CmdFlush(cmd);
                }
                Rhi::PassEnd();
            }

            rhi_texture renderTarget = Rhi::GetTexture(rtv);

            Rhi::CmdTransitionTexture(cmd, renderTarget, rhi_texture_state::TransferSrc);
            Rhi::CmdTransitionTexture(cmd, backbuffer, rhi_texture_state::TransferDst);

            Rhi::CmdCopyTexture(cmd, backbuffer, renderTarget);

            Rhi::CmdTransitionTexture(cmd, backbuffer, rhi_texture_state::Present);
        }

        Rhi::FrameEnd(alloc);

        uint64_t frameEndUs = GetMonotonicTimeMicros();
        uint64_t frameDurationUs = frameEndUs - game->lastFrameStartUs;

        if (game->targetFrameDurationUs > frameDurationUs)
        {
            uint64_t sleepForMillis = (game->targetFrameDurationUs - frameDurationUs) / 1000;
            if (sleepForMillis)
                Sleep(sleepForMillis);
        }
    }
}

} // namespace nyla