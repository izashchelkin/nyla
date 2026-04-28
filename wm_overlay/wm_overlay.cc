#include <ctime>
#include <sys/poll.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "nyla/commons/asset_manager.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/dev_assets.h"
#include "nyla/commons/dev_shaders.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/platform_linux.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/time.h"

namespace nyla
{

void UserMain()
{
    const xcb_window_t window =
        X11CreateWin(X11GetScreen()->width_in_pixels, X11GetScreen()->height_in_pixels, true, XCB_EVENT_MASK_EXPOSURE);
    xcb_configure_window(X11GetConn(), window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_BELOW});
    X11Flush();

    X11SetWindow(window);

    region_alloc alloc = RegionAlloc::Create(4_MiB, 0);

    Rhi::Bootstrap(alloc, {
                              .flags = rhi_flags::VSync,
                              .limits =
                                  {
                                      .numTextures = 0,
                                      .numTextureViews = 0,
                                      .numBuffers = 0,
                                      .numSamplers = 0,
                                      .numFramesInFlight = 1,
                                      .maxDrawCount = 1,
                                      .maxPassCount = 1,
                                      .frameConstantSize = 0,
                                      .passConstantSize = 0,
                                      .drawConstantSize = 0,
                                      .largeDrawConstantSize = 320,
                                  },
                          });

    AssetManager::Bootstrap(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));
#if !defined(NDEBUG)
    {
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

    for (;;)
    {
        RegionAlloc::Reset(alloc);

        rhi_cmdlist cmd = Rhi::FrameBegin(alloc);

        bool shouldRedraw = false;

        auto processEvents = [&] -> void {
            for (;;)
            {
                PlatformEvent event{};
                if (!WinPollEvent(event))
                    break;

                if (event.type == PlatformEventType::Repaint)
                    shouldRedraw = true;
                if (event.type == PlatformEventType::Quit)
                    exit(0);
            }
        };
        processEvents();

        static uint64_t prevUs = GetMonotonicTimeMicros();
        if (!shouldRedraw)
        {
            for (;;)
            {
                const uint64_t now = GetMonotonicTimeMicros();
                const uint64_t diff = now - prevUs;
                if (diff >= 500'000)
                {
                    prevUs = now;
                    break;
                }

                pollfd fd{
                    .fd = xcb_get_file_descriptor(X11GetConn()),
                    .events = POLLIN,
                };

                int pollRes = poll(&fd, 1, Max(1, static_cast<int>(diff / 1000)));
                if (pollRes > 0)
                {
                    processEvents();
                    if (shouldRedraw)
                        break;
                }
                continue;
            }
        }

        time_t t = time(nullptr);
        struct tm *tm = localtime(&t);
        DebugTextRenderer::Fmt(1, 1, "%02d:%02d:%02d %02d.%02d.%04d"_s, tm->tm_hour, tm->tm_min, tm->tm_sec,
                               tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);

        rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());

        Rhi::CmdTransitionTexture(cmd, backbuffer, rhi_texture_state::ColorTarget);

        Rhi::PassBegin({
            .rtv = Rhi::GetBackbufferView(),
            .dsv = {},
        });

        DebugTextRenderer::CmdFlush(cmd);

        Rhi::PassEnd();

        Rhi::CmdTransitionTexture(cmd, backbuffer, rhi_texture_state::Present);

        Rhi::FrameEnd(alloc);
    }
}

} // namespace nyla
