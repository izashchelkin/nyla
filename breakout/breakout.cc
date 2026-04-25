#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/color.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/math.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

const uint64_t kCubeGltfGuid = 0x1077DCB383E4F409;
const uint64_t kCubeBinGuid = 0x7C9E66305CB656C0;

const uint64_t kSphereGltfGuid = 0x831167E33B4E1011;
const uint64_t kSphereBinGuid = 0xDE33DC595E98C184;

const uint64_t kRectGltfGuid = 0x328C6225041A814B;
const uint64_t kRectBinGuid = 0x07EB6974550BDCD0;

const uint64_t kBackgroundGuid = 0x1C3EBA857F103740;
const uint64_t kBallSmallBlueGuid = 0xB0851B6A9FDC5EC3;
const uint64_t kBrick1Guid = 0x536DFA7327C4D3E0;
const uint64_t kBrick2Guid = 0x3111A93E450AEFCF;
const uint64_t kBrick3Guid = 0x361985575F1F6033;
const uint64_t kBrick4Guid = 0x71D9101DC62A152D;
const uint64_t kBrick5Guid = 0x1E86F2389D300F95;
const uint64_t kBrick6Guid = 0x645A2B0CB91DBFDD;
const uint64_t kBrick7Guid = 0xB516416E99DC16A6;
const uint64_t kBrick8Guid = 0xC6AEED02BCC31788;
const uint64_t kBrick9Guid = 0x12DAADBE8487CBB7;
const uint64_t kBrickUnbreakableGuid = 0xDFF1B727573A893D;
const uint64_t kFrameGuid = 0xFB8F0BA1D020EB59;
const uint64_t kPlayerGuid = 0x45B048CC6E76F0AE;
const uint64_t kPlayerFlashGuid = 0x7BA51AB5A7863015;

const uint64_t kBricks[] = {
    kBrick1Guid, kBrick2Guid, kBrick3Guid, kBrick4Guid, kBrick5Guid, kBrick6Guid, kBrick7Guid, kBrick8Guid, kBrick9Guid,
};

struct brick_data
{
    static constexpr uint32_t kFlagDead = 1 << 0;

    uint32_t flags;
    float2 pos;
    float2 size;
    texture_handle texture;
};

struct game_state
{
    uint64_t targetFrameDurationUs;
    uint64_t lastFrameStartUs;
    uint32_t dtUsAccum;
    uint32_t framesCounted;
    uint32_t fps;
    bool shouldExit;

    inline_vec<brick_data, 512> bricks;

    float2 worldBoundaryX;
    float2 worldBoundaryY;
    float ballRadius;
    float playerPosX;
    float playerPosY;
    float playerHeight;
    float playerWidth;
    float2 ballPos;
    float2 ballVel;
};
game_state *game;

auto IsInside(float pos, float size, float2 boundary) -> bool
{
    if (pos > boundary[0] - size / 2.f && pos < boundary[1] + size / 2.f)
    {
        return true;
    }
    return false;
}

} // namespace

void UserMain()
{
    game = &RegionAlloc::Alloc<game_state>(RegionAlloc::g_BootstrapAlloc);
    game->targetFrameDurationUs = 1'000'000 / 144;

    region_alloc alloc = RegionAlloc::Create(16_MiB, 0);

    WinOpen();
    Rhi::Bootstrap(alloc, {
                              .flags = rhi_flags::VSync,
                          });

    InputManager::Bootstrap();
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    MeshManager::Bootstrap();
    TweenManager::Bootstrap();
    DebugTextRenderer::Bootstrap(alloc);
    Renderer::Bootstrap(alloc);
    AssetManager::Bootstrap(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));

    mesh_handle cubeMesh = MeshManager::DeclareMesh(kCubeGltfGuid, kCubeBinGuid);
    mesh_handle sphereMesh = MeshManager::DeclareMesh(kSphereGltfGuid, kSphereBinGuid);
    mesh_handle rectMesh = MeshManager::DeclareMesh(kRectGltfGuid, kRectBinGuid);

    texture_handle backgroundTex = TextureManager::DeclareTexture(kBackgroundGuid);
    texture_handle ballTex = TextureManager::DeclareTexture(kBallSmallBlueGuid);
    texture_handle frameTex = TextureManager::DeclareTexture(kFrameGuid);
    texture_handle playerTex = TextureManager::DeclareTexture(kPlayerGuid);
    texture_handle playerFlashTex = TextureManager::DeclareTexture(kPlayerFlashGuid);
    texture_handle brickUnbreakableTex = TextureManager::DeclareTexture(kBrickUnbreakableGuid);

    array<texture_handle, 9> brickTextures;
    for (int i = 0; i < 9; ++i)
        brickTextures[i] = TextureManager::DeclareTexture(kBricks[i]);

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    { // game init

        game->lastFrameStartUs = GetMonotonicTimeMicros();
        game->worldBoundaryX = {-35.f, 35.f};
        game->worldBoundaryY = {-30.f, 30.f};
        game->ballRadius = .8f;
        game->playerPosX = 0.f;
        game->playerPosY = game->worldBoundaryY[0] + 1.6f;
        game->playerHeight = 2.5f;
        game->playerWidth = (74.f / 26.f) * game->playerHeight;
        game->ballPos = {10.f, game->playerPosY};
        game->ballVel = {40.f, 40.f};

        for (uint32_t i = 0; i < 12; ++i)
        {
            float h = std::fmod(static_cast<float>(i) + 825.f, 12.f) / 12.f;
            float s = .97f;
            float v = .97f;

            float3 color = ConvertHsvToRgb({h, s, v});

            for (uint32_t j = 0; j < 17; ++j)
            {
                float y = 20.f - (float)i * 1.5f;
                float x = -28.f + (float)j * 3.5f;

                brick_data &brick = InlineVec::Append(game->bricks, brick_data{
                                                                        .pos =
                                                                            {
                                                                                50.f * (float)((j % 2) ? 1 : -1),
                                                                                50.f * (float)((j % 3) ? -1 : 1),
                                                                            },
                                                                        .size = {40.f / 15.f, 1.f},
                                                                    });

                TweenManager::Lerp(brick.pos[0], x, TweenManager::Now(), TweenManager::Now() + 1);
                TweenManager::Lerp(brick.pos[1], y, TweenManager::Now(), TweenManager::Now() + 1);
            }
        }

        InputManager::Map(input_id::MoveRight, input_interface_type::Keyboard, (uint32_t)KeyPhysical::F);
        InputManager::Map(input_id::MoveLeft, input_interface_type::Keyboard, (uint32_t)KeyPhysical::S);
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
        MeshManager::Update(alloc, cmd);
        TextureManager::Update(cmd);

        {
            rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
            rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

            rhi_rtv rtv;
            RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, &rtv, nullptr);

            {
                // game input
                const int dx =
                    InputManager::IsPressed(input_id::MoveRight) - InputManager::IsPressed(input_id::MoveLeft);

                // game simulation
                static float dtAccumulator = 0.f;
                dtAccumulator += dt;

                constexpr float kStep = 1.f / 120.f;
                for (; dtAccumulator >= kStep; dtAccumulator -= kStep)
                {
                    game->playerPosX += 100.f * kStep * dx;
                    game->playerPosX = Clamp(game->playerPosX, game->worldBoundaryX[0] + game->playerWidth / 2.f,
                                             game->worldBoundaryX[1] - game->playerWidth / 2.f);

                    if (!IsInside(game->ballPos[0], game->ballRadius * 2.f, game->worldBoundaryX))
                    {
                        game->ballVel[0] = -game->ballVel[0];
                    }

                    if (!IsInside(game->ballPos[1], game->ballRadius * 2.f, game->worldBoundaryY))
                    {
                        game->ballVel[1] = -game->ballVel[1];
                    }

                    // What's happening?
                    // ball_pos[0] = std::clamp(ball_pos[0], kWorldBoundaryX[0] + kBallRadius, kWorldBoundaryX[1] -
                    // kBallRadius); ball_pos[1] = std::clamp(ball_pos[1], kWorldBoundaryY[0] + kBallRadius,
                    // kWorldBoundaryY[1] - kBallRadius);

                    game->ballPos += game->ballVel * kStep;

                    for (auto &brick : game->bricks)
                    {
                        if (brick.flags & brick_data::kFlagDead)
                            continue;

                        bool hit = false;

                        if (IsInside(game->ballPos[0], game->ballRadius * 2.f,
                                     float2{brick.pos[0] - brick.size[0] / 2.f, brick.pos[0] + brick.size[0] / 2.f}))
                        {
                            if (IsInside(
                                    game->ballPos[1], game->ballRadius * 2.f,
                                    float2{brick.pos[1] - brick.size[1] / 2.f, brick.pos[1] + brick.size[1] / 2.f}))
                            {
                                game->ballVel[1] = -game->ballVel[1];
                                game->ballVel[0] = -game->ballVel[0];
                                hit = true;
                                brick.flags |= brick_data::kFlagDead;
                            }
                        }

                        if (hit)
                            break;
                    }

                    {
                        if (IsInside(game->ballPos[0], game->ballRadius * 2.f,
                                     float2{game->playerPosX - game->playerWidth / 2.f,
                                            game->playerPosX + game->playerWidth / 2.f}))
                        {
                            if (IsInside(game->ballPos[1], game->ballRadius * 2.f,
                                         float2{game->playerPosY - game->playerHeight / 2.f,
                                                game->playerPosY + game->playerHeight / 2.f}))
                            {
                                game->ballVel[1] = -game->ballVel[1];
                                game->ballVel[0] = -game->ballVel[0];
                            }
                        }
                    }
                }

                rhi_texture renderTarget = Rhi::GetTexture(rtv);
                rhi_texture_info rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(cmd, renderTarget, rhi_texture_state::ColorTarget);

                Rhi::PassBegin({
                    .rtv = rtv,
                });
                {
                    Renderer::Mesh({0, 0, 0}, {100, 70, 0}, rectMesh, backgroundTex);

                    uint32_t i = 0;
                    for (brick_data &brick : game->bricks)
                    {
                        i++;
                        if (brick.flags & brick_data::kFlagDead)
                            continue;

                        const float3 pos = float3{brick.pos[0], brick.pos[1], 0};
                        const float3 size = float3{brick.size[0], brick.size[1], 0};

                        Renderer::Mesh(pos, size, rectMesh, brickTextures[i % Array::Size(brickTextures)]);
                    }

                    uint64_t second = GetMonotonicTimeMillis() / 1000;
                    if (second % 2)
                    {
                        Renderer::Mesh({game->playerPosX, game->playerPosY, 0},
                                       {game->playerWidth, game->playerHeight, 0}, rectMesh, playerFlashTex);
                    }
                    else
                    {
                        Renderer::Mesh({game->playerPosX, game->playerPosY, 0},
                                       {game->playerWidth, game->playerHeight, 0}, rectMesh, playerTex);
                    }

                    {
                        const float3 pos = float3{game->ballPos[0], game->ballPos[1], 0};
                        Renderer::Mesh(pos, {game->ballRadius * 2, game->ballRadius * 2, 0}, rectMesh, ballTex);
                    }

                    Renderer::SetOrthoProjection(rtInfo.width, rtInfo.height, 64);

                    float4x4 view;
                    Mat::Identity(view);
                    Renderer::SetView(view);

                    Renderer::CmdFlush(cmd);
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