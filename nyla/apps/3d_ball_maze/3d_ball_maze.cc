#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/input_manager.h"
#include "nyla/engine/render_targets.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <utility>

namespace nyla
{

void Game::Init()
{
    auto &assetManager = g_Engine.GetAssetManager();

    m_Assets.ball = assetManager.DeclareMesh("c:/blender/export/sphere.gltf");

    //

    m_RenderTargets.Init(RhiTextureFormat::B8G8R8A8_sRGB, RhiTextureFormat::D32_Float_S8_UINT);
}

void Game::Process(RhiCmdList cmd, float dt)
{
    auto &renderer = g_Engine.GetRenderer();
    auto &inputManager = g_Engine.GetInputManager();
    auto &assetManager = g_Engine.GetAssetManager();
    auto &debugTextRenderer = g_Engine.GetDebugTextRenderer();

    static const auto reloadAssets = inputManager.NewIdMapped(1, uint32_t(KeyPhysical::F5));
    if (inputManager.IsPressed(reloadAssets))
    {
        assetManager.Flush();
        assetManager.Upload(cmd);
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

    if (g_Platform.UpdateGamepad(0))
    {
        movement = g_Platform.GetGamepadLeftStick(0);

        const float2 rotation = g_Platform.GetGamepadRightStick(0);
        yaw += rotation[0] * 0.1f;
        pitch -= rotation[1] * 0.1f;

        pitch = std::clamp(pitch, -1.55f, 1.55f);

        if (yaw > 2 * std::numbers::pi_v<float>)
            yaw -= 2 * std::numbers::pi_v<float>;
        if (yaw < -2 * std::numbers::pi_v<float>)
            yaw += 2 * std::numbers::pi_v<float>;

        verticalInput = g_Platform.GetGamepadLeftTrigger(0) - g_Platform.GetGamepadRightTrigger(0);
    }

    g_Platform.GetGamepadLeftStick(0);

    RhiTexture backbuffer = g_Rhi.GetTexture(g_Rhi.GetBackbufferView());
    RhiTextureInfo backbufferInfo = g_Rhi.GetTextureInfo(backbuffer);

    RhiRenderTargetView rtv;
    RhiDepthStencilView dsv;
    m_RenderTargets.GetTargets(backbufferInfo.width, backbufferInfo.height, rtv, dsv);

    RhiTexture renderTarget = g_Rhi.GetTexture(rtv);
    RhiTextureInfo rtInfo = g_Rhi.GetTextureInfo(renderTarget);
    g_Rhi.CmdTransitionTexture(cmd, renderTarget, RhiTextureState::ColorTarget);

    g_Rhi.CmdTransitionTexture(cmd, g_Rhi.GetTexture(dsv), RhiTextureState::DepthTarget);

    g_Rhi.PassBegin({
        .rtv = rtv,
        .dsv = dsv,
    });
    {
        renderer.Mesh({0, 0, 0}, {1, 1, 1}, m_Assets.ball, {});

        const float3 forward{
            cos(pitch) * sin(yaw),
            sin(pitch),
            cos(pitch) * cos(yaw),
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

        renderer.SetLookAtView(cameraPos, targetPos, worldUp);

        renderer.SetPerspectiveProjection(rtInfo.width, rtInfo.height, 90.f, .01f, 1000.f);

        renderer.CmdFlush(cmd);
        debugTextRenderer.CmdFlush(cmd);
    }
    g_Rhi.PassEnd();

    g_Rhi.CmdTransitionTexture(cmd, renderTarget, RhiTextureState::TransferSrc);
    g_Rhi.CmdTransitionTexture(cmd, backbuffer, RhiTextureState::TransferDst);

    g_Rhi.CmdCopyTexture(cmd, backbuffer, renderTarget);

    g_Rhi.CmdTransitionTexture(cmd, backbuffer, RhiTextureState::Present);
}

//

auto PlatformMain() -> int
{
    g_Platform.Init({
        .enabledFeatures = PlatformFeature::KeyboardInput,
        .open = true,
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, RegionAllocCommitPageGrowth::GetInstance());

    g_Engine.Init({
        .rootAlloc = rootAlloc,
    });

    Game game{};
    game.Init();

    while (!g_Engine.ShouldExit())
    {
        const auto [cmd, dt, fps] = g_Engine.FrameBegin();
        g_Engine.GetDebugTextRenderer().Text(500, 10, std::format("fps={}", fps));

        RhiTextureInfo backbufferInfo = g_Rhi.GetTextureInfo(g_Rhi.GetTexture(g_Rhi.GetBackbufferView()));

        game.Process(cmd, dt);

        g_Engine.FrameEnd();
    }

    return 0;
}

} // namespace nyla