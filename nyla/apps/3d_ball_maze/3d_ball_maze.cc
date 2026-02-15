#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/engine/debug_text_renderer.h"
#include "nyla/engine/engine.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"
#include <format>

namespace nyla
{

void Game::Init()
{
}

void Game::Process(RhiCmdList cmd, float dt)
{
}

void Game::Render(RhiCmdList cmd, RhiRenderTargetView rtv)
{
    g_Rhi.PassBegin({
        .renderTarget = rtv,
        .state = RhiTextureState::ColorTarget,
    });

    RhiTextureInfo rtvInfo = g_Rhi.GetTextureInfo(g_Rhi.GetTexture(rtv));

    auto &renderer = g_Engine.GetRenderer2D();

    renderer.CmdFlush(cmd, rtvInfo.width, rtvInfo.height, 64);
    g_Engine.GetDebugTextRenderer().CmdFlush(cmd);

    g_Rhi.PassEnd({
        .renderTarget = rtv,
        .state = RhiTextureState::Present,
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

        game.Process(cmd, dt);

        RhiRenderTargetView rtv = g_Rhi.GetBackbufferView();
        game.Render(cmd, rtv);

        g_Engine.FrameEnd();
    }

    return 0;
}

} // namespace nyla