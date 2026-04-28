#include <cstdint>

#include "assets.h"
#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/audio.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/color.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/dev_assets.h"
#include "nyla/commons/dev_log.h"
#include "nyla/commons/dev_shaders.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/profiler.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tunables.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

const uint64_t kBricks[] = {
    ID_breakout_brick_1_4, ID_breakout_brick_2_4, ID_breakout_brick_3_4, ID_breakout_brick_4_4, ID_breakout_brick_5_4,
    ID_breakout_brick_6_4, ID_breakout_brick_7_4, ID_breakout_brick_8_4, ID_breakout_brick_9_4,
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
    inline_vec<brick_data, 512> bricks;

    audio_clip_handle brickHitClip;
    audio_clip_handle paddleHitClip;

    float2 worldBoundaryX;
    float2 worldBoundaryY;
    float ballRadius;
    float playerPosX;
    float playerPosY;
    float playerHeight;
    float playerWidth;
    float playerSpeed;
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

    region_alloc alloc = RegionAlloc::Create(16_MiB, 0);

    Engine::Bootstrap(alloc, engine_init_desc{
                                 .maxFps = 144,
                                 .vsync = true,
                             });

    AssetManager::Bootstrap(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    MeshManager::Bootstrap();
    TweenManager::Bootstrap();
#if !defined(NDEBUG)
    {
        DevLog::Bootstrap();

        const byteview devRoots[] = {"assets"_s, "asset_public"_s};
        DevAssets::Bootstrap(span<const byteview>{devRoots, 2});

        const dev_shader_root shaderRoots[] = {
            {.srcDir = "nyla/shaders"_s, .outDir = "asset_public/shaders"_s},
        };
        DevShaders::Bootstrap(span<const dev_shader_root>{shaderRoots, 1});
    }
#endif
    Shader::Bootstrap();
    PipelineCache::Bootstrap();
    DebugTextRenderer::Bootstrap(alloc);
    Renderer::Bootstrap(alloc);
#if !defined(NDEBUG)
    CellRenderer::Bootstrap(alloc, cell_renderer_init_desc{
                                       .bdfGuid = ID_bdf_terminus_u32,
                                   });
    Tunables::Bootstrap("breakout.tunables"_s);
    Tunables::RegisterFloat("ball.radius"_s, &game->ballRadius, 0.05f, 0.1f, 5.f);
    Tunables::RegisterFloat("player.height"_s, &game->playerHeight, 0.1f, 0.5f, 10.f);
    Tunables::RegisterFloat("player.speed"_s, &game->playerSpeed, 5.f, 5.f, 400.f);
    Profiler::Bootstrap();
#endif

    Audio::Bootstrap(48000, 2, 20'000);

    game->brickHitClip = Audio::DeclareClip(ID_breakout_brick_hit_wav);
    game->paddleHitClip = Audio::DeclareClip(ID_breakout_paddle_hit_wav);

    mesh_handle cubeMesh = MeshManager::DeclareMesh(ID_mesh_gltf_cube, ID_mesh_bin_cube);
    mesh_handle sphereMesh = MeshManager::DeclareMesh(ID_mesh_gltf_sphere, ID_mesh_bin_sphere);
    mesh_handle rectMesh = MeshManager::DeclareMesh(ID_mesh_rect_gltf, ID_mesh_bin_rect);

    texture_handle backgroundTex = TextureManager::DeclareTexture(ID_breakout_background1);
    texture_handle ballTex = TextureManager::DeclareTexture(ID_breakout_ball_small_blue);
    texture_handle frameTex = TextureManager::DeclareTexture(ID_breakout_frame);
    texture_handle playerTex = TextureManager::DeclareTexture(ID_breakout_player);
    texture_handle playerFlashTex = TextureManager::DeclareTexture(ID_breakout_player_flash);
    texture_handle brickUnbreakableTex = TextureManager::DeclareTexture(ID_breakout_brick_unbreakable2);

    array<texture_handle, 9> brickTextures;
    for (int i = 0; i < 9; ++i)
        brickTextures[i] = TextureManager::DeclareTexture(kBricks[i]);

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    { // game init
        game->worldBoundaryX = {-35.f, 35.f};
        game->worldBoundaryY = {-30.f, 30.f};
        game->ballRadius = .8f;
        game->playerPosX = 0.f;
        game->playerPosY = game->worldBoundaryY[0] + 1.6f;
        game->playerHeight = 2.5f;
        game->playerWidth = (74.f / 26.f) * game->playerHeight;
        game->playerSpeed = 100.f;
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

                float posX;
                if (j % 2)
                    posX = 50.f;
                else
                    posX = -50.f;
                float posY;
                if (j % 3)
                    posY = -50.f;
                else
                    posY = 50.f;
                brick_data &brick = InlineVec::Append(game->bricks, brick_data{
                                                                        .pos = {posX, posY},
                                                                        .size = {40.f / 15.f, 1.f},
                                                                    });

                TweenManager::Lerp(brick.pos[0], x, TweenManager::Now(), TweenManager::Now() + 1);
                TweenManager::Lerp(brick.pos[1], y, TweenManager::Now(), TweenManager::Now() + 1);
            }
        }

        InputManager::Map(input_id::MoveRight, input_interface_type::Keyboard, (uint32_t)KeyPhysical::F);
        InputManager::Map(input_id::MoveLeft, input_interface_type::Keyboard, (uint32_t)KeyPhysical::S);
    }

    while (!Engine::ShouldExit())
    {
        engine_frame frame = Engine::FrameBegin(alloc);

        DebugTextRenderer::Fmt(500, 10, "fps=%d"_s, uint32_t{frame.fps});

        GpuUpload::Update();
        InputManager::Update();
        TweenManager::Update(frame.dt);
        MeshManager::Update(alloc, frame.cmd);
        TextureManager::Update(frame.cmd);

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
                static uint64_t dtUsAccumulator = 0;
                dtUsAccumulator += frame.dtUs;

                constexpr uint64_t kStepUs = 1'000'000 / 120;
                constexpr float kStep = 1.f / 120.f;
                for (; dtUsAccumulator >= kStepUs; dtUsAccumulator -= kStepUs)
                {
                    game->playerPosX += game->playerSpeed * kStep * dx;
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
                                Audio::Play(game->brickHitClip);
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
                                Audio::Play(game->paddleHitClip);
                            }
                        }
                    }
                }

                rhi_texture renderTarget = Rhi::GetTexture(rtv);
                rhi_texture_info rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::ColorTarget);

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

                    Renderer::CmdFlush(frame.cmd);
                    DebugTextRenderer::CmdFlush(frame.cmd);

#if !defined(NDEBUG)
                    {
                        span<byteview> lines = DevLog::Snapshot(alloc, 8);
                        if (lines.size > 0)
                        {
                            CellRenderer::Begin(8, 8, 100, (uint32_t)lines.size);
                            for (uint64_t i = 0; i < lines.size; ++i)
                                CellRenderer::Text(0, (uint32_t)i, lines[i], 0xFFFF8080u, 0xFF000000u);
                            CellRenderer::CmdFlush(frame.cmd);
                        }
                        Profiler::CmdFlush(frame.cmd, 8, 8 + 32 * 9, frame.fps);
                        Tunables::CmdFlush(frame.cmd, 8, 8 + 32 * 18);
                    }
#endif
                }
                Rhi::PassEnd();
            }

            rhi_texture renderTarget = Rhi::GetTexture(rtv);

            Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::TransferSrc);
            Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::TransferDst);

            Rhi::CmdCopyTexture(frame.cmd, backbuffer, renderTarget);

            Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::Present);
        }

        Engine::FrameEnd(alloc);
    }
}

} // namespace nyla