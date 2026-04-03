#include <format>

#include "nyla/commons/region_alloc.h"
#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"
#include "nyla/apps/3d_ball_maze/scene.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/rhi.h"

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
    RhiTexture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
    RhiTextureInfo backbufferInfo = Rhi::GetTextureInfo(backbuffer);

    RhiRenderTargetView rtv;
    RhiDepthStencilView dsv;
    m_RenderTargets.GetTargets(backbufferInfo.width, backbufferInfo.height, rtv, dsv);

    GameScene scene;
    scene.Process(*this, cmd, dt, rtv, dsv);

    RhiTexture renderTarget = Rhi::GetTexture(rtv);

    Rhi::CmdTransitionTexture(cmd, renderTarget, RhiTextureState::TransferSrc);
    Rhi::CmdTransitionTexture(cmd, backbuffer, RhiTextureState::TransferDst);

    Rhi::CmdCopyTexture(cmd, backbuffer, renderTarget);

    Rhi::CmdTransitionTexture(cmd, backbuffer, RhiTextureState::Present);
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