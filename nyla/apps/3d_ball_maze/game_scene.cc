#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"
#include "nyla/apps/3d_ball_maze/scene.h"
#include "nyla/engine/engine.h"

namespace nyla
{

void GameScene::Process(Game &game, RhiCmdList cmd, float dt, RhiRenderTargetView rtv, RhiDepthStencilView dsv)
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

    RhiTexture renderTarget = g_Rhi.GetTexture(rtv);
    RhiTextureInfo rtInfo = g_Rhi.GetTextureInfo(renderTarget);
    g_Rhi.CmdTransitionTexture(cmd, renderTarget, RhiTextureState::ColorTarget);

    g_Rhi.CmdTransitionTexture(cmd, g_Rhi.GetTexture(dsv), RhiTextureState::DepthTarget);

    g_Rhi.PassBegin({
        .rtv = rtv,
        .dsv = dsv,
    });
    {
        renderer.Mesh({0, 0, 0}, {1, 1, 1}, game.GetAssets().ball, {});
        renderer.Mesh({0, 0, 0}, {1, 1, 1}, game.GetAssets().cube, {});

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
}

} // namespace nyla