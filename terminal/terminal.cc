// https://gist.github.com/jake-stewart/0a8ea46159a7da2c808e5be2177e1783

#include <cstdint>

#include "assets.h"
#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/dev_assets.h"
#include "nyla/commons/dev_shaders.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/lerp.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

constexpr auto PackRGB(uint8_t r, uint8_t g, uint8_t b) -> uint32_t
{
    return (r << 16) | (g << 8) | b;
}

constexpr auto PackRGB(float3 rgb) -> uint32_t
{
    return PackRGB(static_cast<uint8_t>(rgb[0] * 255.f), static_cast<uint8_t>(rgb[1] * 255.f),
                   static_cast<uint8_t>(rgb[2] * 255.f));
}

constexpr void UnpackRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = (color >> 16) & 0xFF;
    g = (color >> 8) & 0xFF;
    b = color & 0xFF;
}

constexpr auto UnpackRGBf(uint32_t color) -> float3
{
    uint8_t r, g, b;
    UnpackRGB(color, r, g, b);

    return {(float)r / 255.f, (float)g / 255.f, (float)b / 255.f};
}

auto RGBToLAB(uint32_t color) -> float3
{
    float3 rgb = UnpackRGBf(color);

    auto pivotRgb = [](float &n) -> void {
        if (n > 0.04045)
            n = std::pow((n + 0.055f) / 1.055f, 2.4f);
        else
            n = n / 12.92f;
    };

    for (uint32_t i = 0; i < 3; ++i)
        pivotRgb(rgb[i]);

    auto pivotXyz = [](float n) -> float {
        if (n > 0.008856f)
            return std::pow(n, (1 / 3.f));
        else
            return (7.787f * n) + (16.f / 116.f);
    };

    float x = pivotXyz(((rgb[0] * 0.4124564f + rgb[1] * 0.3575761f + rgb[2] * 0.1804375f) * 100.0f) / 95.047f);
    float y = pivotXyz(((rgb[0] * 0.2126729f + rgb[1] * 0.7151522f + rgb[2] * 0.0721750f) * 100.0f) / 100.000f);
    float z = pivotXyz(((rgb[0] * 0.0193339f + rgb[1] * 0.1191920f + rgb[2] * 0.9503041f) * 100.0f) / 108.883f);

    float l = Max(0.f, (116.f * y) - 16.f);
    float a = 500.f * (x - y);
    float b = 200.f * (y - z);

    return float3{l, a, b};
}

auto LABToRGB(float3 lab) -> float3
{
    float y = (lab[0] + 16.f) / 116.f;
    float x = lab[1] / 500.f + y;
    float z = y - lab[2] / 200.f;

    auto pivotXyzRev = [](float n) -> float {
        float n3 = std::pow(n, 3.f);
        if (n3 > 0.008856)
            return n3;
        else
            return (n - 16.0f / 116.0f) / 7.787f;
    };

    x = pivotXyzRev(x) * 95.047f / 100.f;
    y = pivotXyzRev(y) * 100.000f / 100.f;
    z = pivotXyzRev(z) * 108.883f / 100.f;

    auto pivotRgbRev = [](float n) -> float {
        n = Max(0.f, Min(1.f, n));
        if (n > 0.0031308f)
            return 1.055f * std::pow(n, (1.f / 2.4f)) - 0.055f;
        else
            return 12.92f * n;
    };

    float r = pivotRgbRev(x * 3.2404542f + y * -1.5371385f + z * -0.4985314f);
    float g = pivotRgbRev(x * -0.9692660f + y * 1.8760108f + z * 0.0415560f);
    float b = pivotRgbRev(x * 0.0556434f + y * -0.2040259f + z * 1.0572252f);

    return {r, g, b};
}

//

// NOLINTBEGIN(bugprone-throwing-static-initialization)
uint32_t Background = PackRGB(28, 28, 28);
uint32_t Foreground = PackRGB(188, 188, 188);

array<uint32_t, 256> Palette{
    PackRGB(28, 28, 28),    // 0: Black
    PackRGB(215, 95, 95),   // 1: Red
    PackRGB(135, 175, 135), // 2: Green
    PackRGB(175, 175, 135), // 3: Yellow
    PackRGB(95, 135, 175),  // 4: Blue
    PackRGB(175, 135, 175), // 5: Magenta
    PackRGB(95, 135, 135),  // 6: Cyan
    PackRGB(188, 188, 188), // 7: White
    PackRGB(118, 118, 118), // 8: Bright Black
    PackRGB(215, 135, 135), // 9: Bright Red
    PackRGB(175, 215, 175), // 10: Bright Green
    PackRGB(215, 215, 175), // 11: Bright Yellow
    PackRGB(135, 175, 215), // 12: Bright Blue
    PackRGB(215, 175, 215), // 13: Bright Magenta
    PackRGB(135, 175, 175), // 14: Bright Cyan
    PackRGB(238, 238, 238), // 15: Bright White
};
// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace

void UserMain()
{
    region_alloc alloc = RegionAlloc::Create(16_MiB, 0);

    Engine::Bootstrap(alloc, engine_init_desc{
                                 .maxFps = 144,
                                 .vsync = true,
                             });

    AssetManager::Bootstrap(FileOpen(R"(assets.bin)"_s, FileOpenMode::Read));
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    MeshManager::Bootstrap();
    TweenManager::Bootstrap();
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
    Renderer::Bootstrap(alloc);
    CellRenderer::Bootstrap(alloc, cell_renderer_init_desc{
                                       .bdfGuid = ID_bdf_terminus_u32,
                                   });

    {
        constexpr bool kHarmonious = false;

        array<float3, 8> base8Lab{
            RGBToLAB(Background), RGBToLAB(Palette[1]), RGBToLAB(Palette[2]), RGBToLAB(Palette[3]),
            RGBToLAB(Palette[4]), RGBToLAB(Palette[5]), RGBToLAB(Palette[6]), RGBToLAB(Foreground),
        };

        bool isLightTheme = base8Lab[7][0] < base8Lab[0][0];

        if constexpr (!kHarmonious)
        {
            if (isLightTheme)
                Swap(base8Lab[0], base8Lab[7]);
        }

        int i = 16;

        for (uint32_t r = 0; r < 6; ++r)
        {
            float3 c0 = Lerp(base8Lab[0], base8Lab[1], static_cast<float>(r) / 5.f);
            float3 c1 = Lerp(base8Lab[2], base8Lab[3], static_cast<float>(r) / 5.f);
            float3 c2 = Lerp(base8Lab[4], base8Lab[5], static_cast<float>(r) / 5.f);
            float3 c3 = Lerp(base8Lab[6], base8Lab[7], static_cast<float>(r) / 5.f);

            for (uint32_t g = 0; g < 6; ++g)
            {
                float3 c4 = Lerp(c0, c1, static_cast<float>(g) / 5.f);
                float3 c5 = Lerp(c2, c3, static_cast<float>(g) / 5.f);

                for (uint32_t b = 0; b < 6; ++b)
                {
                    float3 c6 = Lerp(c4, c5, static_cast<float>(b) / 5.f);
                    Palette[i++] = PackRGB(LABToRGB(c6));
                }
            }
        }

        for (uint32_t j = 0; j < 24; ++j)
        {
            float t = static_cast<float>(i + 1) / 25.f;
            Palette[i++] = PackRGB(LABToRGB(Lerp(base8Lab[0], base8Lab[7], t)));
        }
    }

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    while (!Engine::ShouldExit())
    {
        engine_frame frame = Engine::FrameBegin(alloc);

        DebugTextRenderer::Fmt(500, 10, "fps=%d"_s, uint32_t{frame.fps});

        GpuUpload::Update();
        InputManager::Update();
        TweenManager::Update(frame.dt);
        MeshManager::Update(alloc, frame.cmd);
        TextureManager::Update(frame.cmd);

        {
            rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
            rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

            rhi_rtv rtv;
            RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, &rtv, nullptr);

            {
                rhi_texture renderTarget = Rhi::GetTexture(rtv);
                rhi_texture_info rtInfo = Rhi::GetTextureInfo(renderTarget);
                Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::ColorTarget);

                Rhi::PassBegin({
                    .rtv = rtv,
                });
                {
                    CellRenderer::Begin(16, 16, 80, 24);
                    CellRenderer::Text(0, 0, "nyla cell renderer"_s, 0xFFEEEEEEu, 0xFF1C1C1Cu);
                    CellRenderer::Text(0, 2, "abcdefghijklmnopqrstuvwxyz"_s, 0xFF87AF87u, 0xFF1C1C1Cu);
                    CellRenderer::Text(0, 3, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"_s, 0xFFD7D7AFu, 0xFF1C1C1Cu);
                    CellRenderer::Text(0, 4, "0123456789 !@#$%^&*()_+-=[]{}"_s, 0xFFD7D7AFu, 0xFF1C1C1Cu);
                    CellRenderer::CmdFlush(frame.cmd);

                    DebugTextRenderer::CmdFlush(frame.cmd);
                }
                Rhi::PassEnd();
            }

            rhi_texture renderTarget = Rhi::GetTexture(rtv);

            Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::TransferSrc);
            Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::TransferDst);

            Rhi::CmdCopyTexture(frame.cmd, backbuffer, renderTarget);

            Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::Present);
        }

        Engine::FrameEnd(alloc);
    }
}

} // namespace nyla