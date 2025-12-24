#include "nyla/apps/breakout/breakout.h"
#include "nyla/apps/breakout/world_renderer.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/engine0/debug_text_renderer.h"
#include "nyla/engine0/engine0.h"
#include "nyla/engine0/gpu_resources.h"
#include "nyla/engine0/renderer2d.h"
#include "nyla/engine0/staging_buffer.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pass.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>
#include <sys/types.h>

namespace nyla
{

static auto Main() -> int
{
    LoggingInit();
    SigIntCoreDump();

    PlatformInit({
        .keyboardInput = true,
        .mouseInput = false,
    });
    PlatformWindow window = PlatformCreateWindow();

    Engine0Init({.window = window});

    PlatformMapInputBegin();
    PlatformMapInput(kLeft, KeyPhysical::S);
    PlatformMapInput(kRight, KeyPhysical::F);
    PlatformMapInput(kFire, KeyPhysical::J);
    PlatformMapInput(kBoost, KeyPhysical::K);
    PlatformMapInputEnd();

    BreakoutInit();

#if 0
    std::array<RhiTexture, kRhiMaxNumFramesInFlight> offscreenTextures;
    for (uint32_t i = 0; i < RhiGetNumFramesInFlight(); ++i)
    {
        offscreenTextures[i] = RhiCreateTexture(RhiTextureDesc{
            .width = 1000,
            .height = 1000,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::ColorTarget | RhiTextureUsage::ShaderSampled,
            .format = RhiTextureFormat::B8G8R8A8_sRGB,
        });
    }

    if (RecompileShadersIfNeeded())
    {
        RpInit(worldPipeline);
        RpInit(guiPipeline);
        RpInit(dbgTextPipeline);
    }

    RhiTexture offscreenTexture = offscreenTextures[RhiGetFrameIndex()];
#endif

    GpuResourcesInit();

    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc *texPixels = stbi_load("assets/BBreaker/Player.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    Texture texture = CreateTexture(texWidth, texHeight, RhiTextureFormat::R8G8B8A8_sRGB);
    Sampler sampler = CreateSampler();

    GpuStagingBuffer *stagingBuffer = CreateStagingBuffer(1 << 20);

    Renderer2D *renderer2d = CreateRenderer2D();
    DebugTextRenderer *debugTextRenderer = CreateDebugTextRenderer();

    while (!Engine0ShouldExit())
    {
        RhiCmdList cmd = Engine0FrameBegin();

        static bool inited = false;
        if (inited)
        {
            DebugText(500, 10, std::format("fps={}", Engine0GetFps()));

            const float dt = Engine0GetDt();
            BreakoutProcess(dt);

            StagingBufferReset(stagingBuffer);
            Renderer2DFrameBegin(cmd, renderer2d, stagingBuffer);

            RhiTexture colorTarget = RhiGetBackbufferTexture();
            RhiTextureInfo colorTargetInfo = RhiGetTextureInfo(colorTarget);

            RhiPassBegin({
                .colorTarget = RhiGetBackbufferTexture(),
                .state = RhiTextureState::ColorTarget,
            });

            BreakoutRenderGame(cmd, renderer2d, colorTargetInfo);

            Renderer2DDraw(cmd, renderer2d, colorTargetInfo.width, colorTargetInfo.height, 64);
            DebugTextRendererDraw(cmd, debugTextRenderer);

            RhiPassEnd({
                .colorTarget = RhiGetBackbufferTexture(),
                .state = RhiTextureState::Present,
            });
        }
        else
        {
            UploadTexture(cmd, stagingBuffer, texture,
                          std::span{reinterpret_cast<const std::byte *>(texPixels),
                                    static_cast<size_t>(texWidth * texHeight * 4)});
            inited = true;

            RhiPassBegin({
                .colorTarget = RhiGetBackbufferTexture(),
                .state = RhiTextureState::ColorTarget,
            });
            RhiPassEnd({
                .colorTarget = RhiGetBackbufferTexture(),
                .state = RhiTextureState::Present,
            });
        }

        GpuResourcesWriteDescriptors();
        Engine0FrameEnd();
    }

    return 0;
}

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}