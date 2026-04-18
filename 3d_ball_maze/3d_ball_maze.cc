#include "3d_ball_maze/3d_ball_maze.h"

#include <cstdint>

#include "nyla/commons/asset_file.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/word.h"

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

    region_alloc alloc = RegionAlloc::Create(16_MiB, 0);

    Rhi::Init(alloc, {
                         .flags = rhi_flags::VSync,
                     });

    GpuUpload::Bootstrap();
    TextureManager::Bootstrap();
    MeshManager::Bootstrap();

    TweenManager::Bootstrap();

    DebugTextRender::Bootstrap(alloc);
    Renderer::Bootstrap(alloc);

    byteview assetFile = AssetFileLoad(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));

    mesh cubeMesh = MeshManager::DeclareMesh(assetFile, kCubeGltfGuid, kCubeBinGuid);
    mesh sphereMesh = MeshManager::DeclareMesh(assetFile, kSphereGltfGuid, kSphereBinGuid);

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

        if (game->dtUsAccum >= 500'000ull)
        {
            const double seconds = static_cast<double>(game->dtUsAccum) / 1'000'000.0;
            const double fpsF64 = game->framesCounted / seconds;

            game->fps = LRound(fpsF64);
            game->dtUsAccum = 0;
            game->framesCounted = 0;
        }

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
            }
        }

        InputManager::Update();
        TweenManager::Update(dt);
        GpuUpload::Update();
        TextureManager::Update(cmd, assetFile);
        MeshManager::Update(alloc, cmd, assetFile);

        {
            rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
            rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

            rhi_rtv rtv;
            rhi_dsv dsv;
            RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, rtv, dsv);

            {
                static const auto reloadAssets = InputManager::NewIdMapped(1, uint32_t(KeyPhysical::F5));
                if (InputManager::IsPressed(reloadAssets))
                {
                    AssetManager::Flush();
                    AssetManager::Upload(cmd);
                }

#if 0
                static const auto moveLeft = inputManager.NewIdMapped(1, uint32_t(KeyPhysical::S));
                static const auto moveRight = inputManager.NewIdMapped(1, uint32_t(KeyPhysical::F));
                static const auto moveForward = inputManager.NewIdMapped(1, uint32_t(KeyPhysical::E));
                static const auto moveBackward = inputManager.NewIdMapped(1, uint32_t(KeyPhysical::D));

                int dx = inputManager.IsPressed(moveRight) - inputManager.IsPressed(moveLeft);
                int dy = inputManager.IsPressed(moveForward) - inputManager.IsPressed(moveBackward);
#endif
                float2 movement{};

                static float yaw = 0;
                static float pitch = 0;

                float verticalInput = 0;

                if (Platform::UpdateGamepad(0))
                {
                    movement = Platform::GetGamepadLeftStick(0);

                    const float2 rotation = Platform::GetGamepadRightStick(0);
                    yaw += rotation[0] * 0.1f;
                    pitch -= rotation[1] * 0.1f;

                    pitch = std::clamp(pitch, -1.55f, 1.55f);

                    if (yaw > 2 * std::numbers::pi_v<float>)
                        yaw -= 2 * std::numbers::pi_v<float>;
                    if (yaw < -2 * std::numbers::pi_v<float>)
                        yaw += 2 * std::numbers::pi_v<float>;

                    verticalInput = Platform::GetGamepadLeftTrigger(0) - Platform::GetGamepadRightTrigger(0);
                }

                RhiTexture renderTarget = Rhi::GetTexture(rtv);
                RhiTextureInfo rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(cmd, renderTarget, RhiTextureState::ColorTarget);

                Rhi::CmdTransitionTexture(cmd, Rhi::GetTexture(dsv), RhiTextureState::DepthTarget);

                Rhi::PassBegin({
                    .rtv = rtv,
                    .dsv = dsv,
                });
                {
                    Renderer::Mesh({0, 0, 0}, {1, 1, 1}, game.GetAssets().ball, {});
                    Renderer::Mesh({0, 0, 0}, {1, 1, 1}, game.GetAssets().cube, {});

                    const float3 forward{
                        std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch),
                        std::cos(pitch) * std::cos(yaw),
                    };

                    static float3 cameraPos{0.f, 0.f, 5.f};

                    const float3 worldUp = {0.0f, 1.0f, 0.0f};
                    const float3 right = worldUp.Cross(forward).Normalized();
                    const float3 up = forward.Cross(right);
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

        uint64_t sleepForMillis = (game->targetFrameDurationUs - frameDurationUs) / 1000;
        if (sleepForMillis)
            Sleep(sleepForMillis);
    }
}

//

auto PlatformMain(Span<const char *> argv) -> int
{
    Platform::Init({
        .enabledFeatures = PlatformFeature::Gfx | PlatformFeature::KeyboardInput,
        .open = true,
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    Engine::Init({
        .rootAlloc = &rootAlloc,
    });

    Game game{};
    game.Init();

    while (!Engine::ShouldExit())
    {
        const auto [cmd, dt, fps] = Engine::FrameBegin();
        DebugTextRenderer::Fmt(500, 10, "fps=%d", fps);

        RhiTextureInfo backbufferInfo = Rhi::GetTextureInfo(Rhi::GetTexture(Rhi::GetBackbufferView()));

        game.Process(cmd, dt);

        Engine::FrameEnd();
    }

    return 0;
}

} // namespace nyla