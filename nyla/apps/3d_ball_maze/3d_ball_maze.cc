#include <format>

#include "nyla/alloc/region_alloc.h"
#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"
#include "nyla/apps/3d_ball_maze/scene.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/render_targets.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

void Game::Init()
{
    m_Assets.ball = AssetManager::DeclareMesh("c:/blender/export/sphere.gltf");
    m_Assets.cube = AssetManager::DeclareMesh("c:/blender/export/cube.gltf");

    //

    m_RenderTargets.Init(RhiTextureFormat::B8G8R8A8_sRGB, RhiTextureFormat::D32_Float_S8_UINT);
}

void Game::Process(RhiCmdList cmd, float dt)
{
    RhiTexture backbuffer = g_Rhi.GetTexture(g_Rhi.GetBackbufferView());
    RhiTextureInfo backbufferInfo = g_Rhi.GetTextureInfo(backbuffer);

    RhiRenderTargetView rtv;
    RhiDepthStencilView dsv;
    m_RenderTargets.GetTargets(backbufferInfo.width, backbufferInfo.height, rtv, dsv);

    GameScene scene;
    scene.Process(*this, cmd, dt, rtv, dsv);

    RhiTexture renderTarget = g_Rhi.GetTexture(rtv);

    g_Rhi.CmdTransitionTexture(cmd, renderTarget, RhiTextureState::TransferSrc);
    g_Rhi.CmdTransitionTexture(cmd, backbuffer, RhiTextureState::TransferDst);

    g_Rhi.CmdCopyTexture(cmd, backbuffer, renderTarget);

    g_Rhi.CmdTransitionTexture(cmd, backbuffer, RhiTextureState::Present);
}

//

auto PlatformMain(std::span<const char *> argv) -> int
{
    Platform::InitGraphical({
        .enabledFeatures = PlatformFeature::KeyboardInput,
        .open = true,
    });

    RegionAlloc rootAlloc;
    rootAlloc.Init(nullptr, 64_GiB, true);

    Engine::Init({
        .rootAlloc = rootAlloc,
    });

    Game game{};
    game.Init();

    while (!Engine::ShouldExit())
    {
        const auto [cmd, dt, fps] = Engine::FrameBegin();
        DebugTextRenderer::Fmt(500, 10, "fps=%d", fps);

        RhiTextureInfo backbufferInfo = g_Rhi.GetTextureInfo(g_Rhi.GetTexture(g_Rhi.GetBackbufferView()));

        game.Process(cmd, dt);

        Engine::FrameEnd();
    }

    return 0;
}

} // namespace nyla