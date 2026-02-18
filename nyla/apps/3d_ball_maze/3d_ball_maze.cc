#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/render_targets.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <format>

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
}

void Game::Render(RhiCmdList cmd)
{
    RhiTextureInfo backbufferInfo = g_Rhi.GetTextureInfo(g_Rhi.GetTexture(g_Rhi.GetBackbufferView()));

    RhiRenderTargetView rtv;
    RhiDepthStencilView dsv;
    m_RenderTargets.GetTargets(backbufferInfo.width, backbufferInfo.height, rtv, dsv);

    g_Rhi.PassBegin({
        .rtv = rtv,
        .rtState = RhiTextureState::ColorTarget,
        .dsv = dsv,
        .dsState = RhiTextureState::DepthTarget,
    });

    RhiTextureInfo rtvInfo = g_Rhi.GetTextureInfo(g_Rhi.GetTexture(rtv));

    auto &renderer = g_Engine.GetRenderer();

    renderer.Mesh({0, 0, 0}, {10, 10, 10}, m_Assets.ball, {});

    renderer.CmdFlush(cmd, rtvInfo.width, rtvInfo.height, 64);
    g_Engine.GetDebugTextRenderer().CmdFlush(cmd);

    g_Rhi.PassEnd({
        .rtv = rtv,
        .rtState = RhiTextureState::Present,
        .dsv = dsv,
        .dsState = RhiTextureState::DepthTarget,
    });
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
        game.Render(cmd);

        g_Engine.FrameEnd();
    }

    return 0;
}

} // namespace nyla