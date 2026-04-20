#include "3d_ball_maze/3d_ball_maze.h"

#include <cstdint>
#include <locale>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
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

const uint64_t kCubeGltfGuid = 0x1077DCB383E4F409;
const uint64_t kCubeBinGuid = 0x7C9E66305CB656C0;

const uint64_t kSphereGltfGuid = 0x831167E33B4E1011;
const uint64_t kSphereBinGuid = 0xDE33DC595E98C184;

struct game_state
{
    uint64_t targetFrameDurationUs;
    uint64_t lastFrameStartUs;
    uint32_t dtUsAccum;
    uint32_t framesCounted;
    uint32_t fps;

    bool shouldExit;
};
game_state *game;

void UserMain()
{
    game = &RegionAlloc::Alloc<game_state>(RegionAlloc::g_BootstrapAlloc);
    MemZero(game);
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

    byteview assetFile = AssetFileLoad(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));

    mesh_handle cubeMesh = MeshManager::DeclareMesh(assetFile, kCubeGltfGuid, kCubeBinGuid);
    mesh_handle sphereMesh = MeshManager::DeclareMesh(assetFile, kSphereGltfGuid, kSphereBinGuid);

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

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
                std::exit(0);
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
        MeshManager::Update(alloc, cmd, assetFile);
        TextureManager::Update(cmd, assetFile);

        {
            rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
            rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

            rhi_rtv rtv;
            rhi_dsv dsv;
            RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, rtv, dsv);

            {
                float2 movement{};

                static float yaw = 0;
                static float pitch = 0;

                float verticalInput = 0;

                if (UpdateGamepad(0))
                {
                    movement = GetGamepadLeftStick(0);

                    const float2 rotation = GetGamepadRightStick(0);
                    yaw += rotation[0] * 0.1f;
                    pitch -= rotation[1] * 0.1f;

                    pitch = Clamp(pitch, -1.55f, 1.55f);

                    if (yaw > 2 * math::pi)
                        yaw -= 2 * math::pi;
                    if (yaw < -2 * math::pi)
                        yaw += 2 * math::pi;

                    verticalInput = GetGamepadLeftTrigger(0) - GetGamepadRightTrigger(0);
                }

                rhi_texture renderTarget = Rhi::GetTexture(rtv);
                rhi_texture_info rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(cmd, renderTarget, rhi_texture_state::ColorTarget);

                Rhi::CmdTransitionTexture(cmd, Rhi::GetTexture(dsv), rhi_texture_state::DepthTarget);

                Rhi::PassBegin({
                    .rtv = rtv,
                    .dsv = dsv,
                });
                {
                    Renderer::Mesh({0, 0, 0}, {1, 1, 1}, sphereMesh, {});
                    Renderer::Mesh({0, 0, 0}, {1, 1, 1}, cubeMesh, {});

                    const float3 forward{
                        std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch),
                        std::cos(pitch) * std::cos(yaw),
                    };

                    static float3 cameraPos{0.f, 0.f, 5.f};

                    const float3 worldUp = {0.0f, 1.0f, 0.0f};
                    const float3 right = Vec::Normalized(Vec::Cross(worldUp, forward));
                    const float3 up = Vec::Normalized(Vec::Cross(forward, right));
                    const float moveSpeed = 10.0f * dt;

                    cameraPos += forward * movement[1] * moveSpeed;
                    cameraPos += right * movement[0] * moveSpeed;
                    cameraPos += worldUp * verticalInput * moveSpeed;

                    const float3 targetPos = cameraPos + forward;

                    Renderer::SetLookAtView(cameraPos, targetPos, worldUp);

                    Renderer::SetPerspectiveProjection(rtInfo.width, rtInfo.height, 90.f, .01f, 1000.f);

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