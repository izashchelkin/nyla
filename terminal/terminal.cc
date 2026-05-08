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
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mem.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/platform_pty.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/terminal_screen.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

constexpr auto PackRGB(uint8_t r, uint8_t g, uint8_t b) -> uint32_t
{
    return (b << 16) | (g << 8) | r;
}

constexpr auto PackRGB(float3 rgb) -> uint32_t
{
    return PackRGB(static_cast<uint8_t>(rgb[0] * 255.f), static_cast<uint8_t>(rgb[1] * 255.f),
                   static_cast<uint8_t>(rgb[2] * 255.f));
}

constexpr void UnpackRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = color & 0xFF;
    g = (color >> 8) & 0xFF;
    b = (color >> 16) & 0xFF;
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

    constexpr int32_t kOriginPxX = 16;
    constexpr int32_t kOriginPxY = 16;
    constexpr uint32_t kInitialCols = 80;
    constexpr uint32_t kInitialRows = 24;

    uint32_t curCols = kInitialCols;
    uint32_t curRows = kInitialRows;

    uint32_t defaultFgRgba = 0xFF000000u | (Foreground & 0x00FFFFFFu);
    uint32_t defaultBgRgba = 0xFF000000u | (Background & 0x00FFFFFFu);

    auto *screen = TerminalScreen::Create(RegionAlloc::g_BootstrapAlloc, terminal_screen_init_desc{
                                                                             .cols = curCols,
                                                                             .rows = curRows,
                                                                             .scrollbackLines = 10000,
                                                                             .defaultFgRgba = defaultFgRgba,
                                                                             .defaultBgRgba = defaultBgRgba,
                                                                             .palette256 = Palette,
                                                                         });

#if defined(_WIN32)
    auto shellPath = "pwsh.exe"_s;
#else
    auto shellPath = "/bin/bash"_s;
#endif

    auto *pty = PlatformPty::Create(RegionAlloc::g_BootstrapAlloc, platform_pty_spawn_desc{
                                                                       .shellPath = shellPath,
                                                                       .cols = curCols,
                                                                       .rows = curRows,
                                                                   });

    array<uint8_t, 8192> readBuf{};

    uint32_t scrollOffset = 0; // lines viewport is scrolled up into scrollback
    bool showStats = false;

    WinSetTitle("hello world!"_s);

    while (!Engine::ShouldExit())
    {
        engine_frame frame = Engine::FrameBegin(alloc);

        if (showStats)
            DebugTextRenderer::Fmt(500, 10, "fps=%d"_s, uint32_t{frame.fps});

        GpuUpload::Update();
        InputManager::Update();
        TweenManager::Update(frame.dt);
        MeshManager::Update(alloc, frame.cmd);
        TextureManager::Update(frame.cmd);

        if (Engine::IsWindowResized())
        { // Resize cells + pty to fit current window. Reserve the origin margin on each axis.
            PlatformWindowSize ws = WinGetSize();
            uint32_t cellW = CellRenderer::CellPxW();
            uint32_t cellH = CellRenderer::CellPxH();
            int32_t availW = (int32_t)ws.width - 2 * kOriginPxX;
            int32_t availH = (int32_t)ws.height - 2 * kOriginPxY;
            uint32_t newCols = (availW > 0 && cellW > 0) ? (uint32_t)availW / cellW : 1;
            uint32_t newRows = (availH > 0 && cellH > 0) ? (uint32_t)availH / cellH : 1;
            if (newCols < 1)
                newCols = 1;
            if (newRows < 1)
                newRows = 1;
            if (newCols != curCols || newRows != curRows)
            {
                TerminalScreen::Resize(*screen, newCols, newRows);
                PlatformPty::Resize(*pty, newCols, newRows);
                curCols = newCols;
                curRows = newRows;
                scrollOffset = 0;
            }
        }

        if (frame.textChars.size > 0)
        {
            // Any keystroke that produces text snaps viewport to live region.
            scrollOffset = 0;
            // xkb maps Backspace to 0x08 ('\b'), but xterm-compatible terminfo expects 0x7F.
            // Substitute in place into a frame-allocated buffer.
            uint8_t *out = (uint8_t *)RegionAlloc::Alloc(alloc, frame.textChars.size, 1);
            for (uint64_t i = 0; i < frame.textChars.size; ++i)
            {
                uint8_t b = frame.textChars.data[i];
                out[i] = (b == 0x08) ? 0x7F : b;
            }
            PlatformPty::Write(*pty, byteview{out, frame.textChars.size});
        }

        {
            constexpr uint32_t kWheelUpBit = 1u << 3;   // button 4
            constexpr uint32_t kWheelDownBit = 1u << 4; // button 5
            constexpr uint32_t kWheelStep = 3;

            terminal_mouse_mode mouseMode = TerminalScreen::MouseMode(*screen);
            bool reportMouse = (mouseMode != terminal_mouse_mode::None);

            if (!reportMouse)
            {
                if (frame.pointerPress & kWheelUpBit)
                {
                    uint32_t cap = TerminalScreen::ScrollbackCount(*screen);
                    if (scrollOffset + kWheelStep > cap)
                        scrollOffset = cap;
                    else
                        scrollOffset += kWheelStep;
                }
                if (frame.pointerPress & kWheelDownBit)
                {
                    scrollOffset = (scrollOffset > kWheelStep) ? scrollOffset - kWheelStep : 0;
                }
            }
        }

        {
            terminal_mouse_mode mouseMode = TerminalScreen::MouseMode(*screen);
            terminal_mouse_format mouseFormat = TerminalScreen::MouseFormat(*screen);

            if (mouseMode != terminal_mouse_mode::None)
            {
                auto report = [&](uint32_t btn, bool press) {
                    int32_t col = (frame.pointerX - kOriginPxX) / (int32_t)CellRenderer::CellPxW();
                    int32_t row = (frame.pointerY - kOriginPxY) / (int32_t)CellRenderer::CellPxH();
                    if (col < 0)
                        col = 0;
                    if (row < 0)
                        row = 0;
                    if (col >= (int32_t)curCols)
                        col = (int32_t)curCols - 1;
                    if (row >= (int32_t)curRows)
                        row = (int32_t)curRows - 1;

                    uint8_t buf[64];
                    uint64_t len = 0;
                    if (mouseFormat == terminal_mouse_format::Sgr)
                    {
                        len = StringWriteFmt(span<uint8_t>{buf, 64}, "\x1b[<%d;%d;%d%c"_s, btn, col + 1, row + 1,
                                             press ? 'M' : 'm');
                    }
                    else
                    {
                        // Default X10/Normal format: CSI M <btn+32> <x+32> <y+32>
                        buf[len++] = 0x1B;
                        buf[len++] = '[';
                        buf[len++] = 'M';
                        buf[len++] = (uint8_t)(32 + btn);
                        buf[len++] = (uint8_t)(32 + col + 1);
                        buf[len++] = (uint8_t)(32 + row + 1);
                    }
                    PlatformPty::Write(*pty, byteview{buf, len});
                };

                // Pointer buttons: 1=Left, 2=Middle, 3=Right, 4=WheelUp, 5=WheelDown.
                for (uint32_t b = 1; b <= 5; ++b)
                {
                    uint32_t bit = 1u << (b - 1);
                    if (frame.pointerPress & bit)
                    {
                        uint32_t btn = (b <= 3) ? b - 1 : (b == 4) ? 64 : 65;
                        report(btn, true);
                    }
                    if (frame.pointerRelease & bit)
                    {
                        if (mouseMode >= terminal_mouse_mode::Normal)
                        {
                            uint32_t btn = (b <= 3) ? b - 1 : (b == 4) ? 64 : 65;
                            report(btn, false);
                        }
                    }
                }

                if (mouseMode >= terminal_mouse_mode::ButtonEvent)
                {
                    if (frame.pointerDx != 0 || frame.pointerDy != 0)
                    {
                        // Motion with button held.
                        if (frame.pointerButtons & 0x7)
                        {
                            uint32_t btn = 32; // Motion flag
                            if (frame.pointerButtons & 1)
                                btn += 0;
                            else if (frame.pointerButtons & 2)
                                btn += 1;
                            else if (frame.pointerButtons & 4)
                                btn += 2;
                            report(btn, true);
                        }
                        else if (mouseMode == terminal_mouse_mode::AnyEvent)
                        {
                            report(35, true); // Motion without buttons
                        }
                    }
                }
            }
        }

        for (uint64_t i = 0; i < frame.keyDown.size; ++i)
        {
            const key_event &ev = frame.keyDown.data[i];

            // F8 owns the stats overlay toggle; don't forward to pty.
            if (ev.key == KeyPhysical::F8 && !(ev.mods & (KeyMod::Shift | KeyMod::Alt | KeyMod::Ctrl)))
            {
                showStats = !showStats;
                continue;
            }

            if (ev.key == KeyPhysical::V && (ev.mods & KeyMod::Ctrl) && (ev.mods & KeyMod::Shift))
            {
                region_alloc tmp = RegionAlloc::Create(1_MiB, 0);
                byteview clip = WinGetClipboard(tmp);
                if (clip.size > 0)
                    PlatformPty::Write(*pty, clip);
                RegionAlloc::Destroy(tmp);
                continue;
            }

            // PageUp/PageDown (with or without Shift) moves the viewport into scrollback;
            // not forwarded to pty. nvim handlers can use Ctrl+f/Ctrl+b instead.
            if (ev.key == KeyPhysical::PageUp || ev.key == KeyPhysical::PageDown)
            {
                uint32_t step = curRows > 1 ? curRows - 1 : 1;
                if (ev.key == KeyPhysical::PageUp)
                {
                    uint32_t cap = TerminalScreen::ScrollbackCount(*screen);
                    if (scrollOffset + step > cap)
                        scrollOffset = cap;
                    else
                        scrollOffset += step;
                }
                else
                {
                    scrollOffset = (scrollOffset > step) ? scrollOffset - step : 0;
                }
                continue;
            }

            // mods param per xterm/csi: 1 + Shift + 2*Alt + 4*Ctrl. 0 means no encoded mods.
            uint8_t modParam = 0;
            if (ev.mods & (KeyMod::Shift | KeyMod::Alt | KeyMod::Ctrl))
                modParam = 1 + ((ev.mods & KeyMod::Shift) ? 1 : 0) + ((ev.mods & KeyMod::Alt) ? 2 : 0) +
                           ((ev.mods & KeyMod::Ctrl) ? 4 : 0);

            // Final letter for arrow / Home / End. Tilde-style (e.g. PgUp) handled below.
            char finalLetter = 0;
            uint8_t tildeNum = 0;        // CSI <n>~ form when nonzero
            const char *fkSeq = nullptr; // ESC O<x> for F1-F4
            uint8_t fkTilde = 0;         // CSI <n>~ for F5-F12
            // DECCKM: app-cursor-keys mode swaps CSI for SS3 on the cursor/Home/End set.
            bool useSS3 = TerminalScreen::ApplicationCursorKeys(*screen);

            switch (ev.key)
            {
            case KeyPhysical::ArrowUp:
                finalLetter = 'A';
                break;
            case KeyPhysical::ArrowDown:
                finalLetter = 'B';
                break;
            case KeyPhysical::ArrowRight:
                finalLetter = 'C';
                break;
            case KeyPhysical::ArrowLeft:
                finalLetter = 'D';
                break;
            case KeyPhysical::Home:
                finalLetter = 'H';
                break;
            case KeyPhysical::End:
                finalLetter = 'F';
                break;
            case KeyPhysical::Insert:
                tildeNum = 2;
                break;
            case KeyPhysical::Delete:
                tildeNum = 3;
                break;
            case KeyPhysical::PageUp:
                tildeNum = 5;
                break;
            case KeyPhysical::PageDown:
                tildeNum = 6;
                break;
            case KeyPhysical::F1:
                fkSeq = "\x1bOP";
                break;
            case KeyPhysical::F2:
                fkSeq = "\x1bOQ";
                break;
            case KeyPhysical::F3:
                fkSeq = "\x1bOR";
                break;
            case KeyPhysical::F4:
                fkSeq = "\x1bOS";
                break;
            case KeyPhysical::F5:
                fkTilde = 15;
                break;
            case KeyPhysical::F6:
                fkTilde = 17;
                break;
            case KeyPhysical::F7:
                fkTilde = 18;
                break;
            case KeyPhysical::F8:
                fkTilde = 19;
                break;
            case KeyPhysical::F9:
                fkTilde = 20;
                break;
            case KeyPhysical::F10:
                fkTilde = 21;
                break;
            case KeyPhysical::F11:
                fkTilde = 23;
                break;
            case KeyPhysical::F12:
                fkTilde = 24;
                break;
            default:
                break;
            }

            uint8_t buf[16];
            uint32_t n = 0;

            auto emitInt = [&](uint32_t v) {
                if (v == 0)
                {
                    buf[n++] = '0';
                    return;
                }
                char tmp[8];
                uint32_t t = 0;
                while (v > 0)
                {
                    tmp[t++] = char('0' + (v % 10));
                    v /= 10;
                }
                while (t > 0)
                    buf[n++] = (uint8_t)tmp[--t];
            };

            if (finalLetter)
            {
                buf[n++] = 0x1B;
                // mods force CSI form even in DECCKM (xterm convention).
                if (useSS3 && !modParam)
                {
                    buf[n++] = 'O';
                }
                else
                {
                    buf[n++] = '[';
                    if (modParam)
                    {
                        buf[n++] = '1';
                        buf[n++] = ';';
                        emitInt(modParam);
                    }
                }
                buf[n++] = (uint8_t)finalLetter;
            }
            else if (tildeNum)
            {
                buf[n++] = 0x1B;
                buf[n++] = '[';
                emitInt(tildeNum);
                if (modParam)
                {
                    buf[n++] = ';';
                    emitInt(modParam);
                }
                buf[n++] = '~';
            }
            else if (fkSeq)
            {
                while (fkSeq[n])
                {
                    buf[n] = (uint8_t)fkSeq[n];
                    ++n;
                }
            }
            else if (fkTilde)
            {
                buf[n++] = 0x1B;
                buf[n++] = '[';
                emitInt(fkTilde);
                if (modParam)
                {
                    buf[n++] = ';';
                    emitInt(modParam);
                }
                buf[n++] = '~';
            }

            if (n > 0)
                PlatformPty::Write(*pty, byteview{buf, n});
        }

        uint64_t feedBytes = 0;
        uint64_t feedStartUs = GetMonotonicTimeMicros();
        for (;;)
        {
            uint32_t got = PlatformPty::Read(*pty, readBuf);
            if (got == 0)
                break;
            feedBytes += got;
            TerminalScreen::Feed(*screen, byteview{readBuf.data, got});
        }

        byteview reply = TerminalScreen::PollReply(*screen);
        if (reply.size > 0)
            PlatformPty::Write(*pty, reply);

        byteview title = TerminalScreen::PollTitle(*screen);
        if (title.size > 0)
            WinSetTitle(title);

        uint64_t feedUs = GetMonotonicTimeMicros() - feedStartUs;
        if (showStats)
            DebugTextRenderer::Fmt(500, 30, "pty=%dB feed=%dus"_s, (uint32_t)feedBytes, (uint32_t)feedUs);

        if (!PlatformPty::IsAlive(*pty))
            Engine::RequestExit();

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
                    CellRenderer::Begin(kOriginPxX, kOriginPxY, curCols, curRows);

                    uint64_t paintStartUs = GetMonotonicTimeMicros();

                    uint32_t curRow = TerminalScreen::CursorRow(*screen);
                    uint32_t curCol = TerminalScreen::CursorCol(*screen);
                    bool curVisible = TerminalScreen::CursorVisible(*screen);
                    terminal_cursor_style curStyle = TerminalScreen::CursorStyle(*screen);
                    if (curVisible && ((uint32_t)curStyle % 2 == 0))
                        curVisible = (GetMonotonicTimeMillis() / 500) % 2 == 0;

                    uint32_t scrollbackCount = TerminalScreen::ScrollbackCount(*screen);
                    if (scrollOffset > scrollbackCount)
                        scrollOffset = scrollbackCount;

                    auto brighten = [](uint32_t rgba) -> uint32_t {
                        uint32_t a = rgba & 0xFF000000u;
                        uint32_t r8 = rgba & 0xFFu;
                        uint32_t g8 = (rgba >> 8) & 0xFFu;
                        uint32_t b8 = (rgba >> 16) & 0xFFu;
                        auto bump = [](uint32_t v) {
                            uint32_t x = (v * 7) / 5; // *1.4
                            return x > 255 ? 255 : x;
                        };
                        return a | (bump(b8) << 16) | (bump(g8) << 8) | bump(r8);
                    };

                    auto dim = [](uint32_t rgba) -> uint32_t {
                        uint32_t a = rgba & 0xFF000000u;
                        uint32_t r8 = rgba & 0xFFu;
                        uint32_t g8 = (rgba >> 8) & 0xFFu;
                        uint32_t b8 = (rgba >> 16) & 0xFFu;
                        return a | (((b8 * 2) / 3) << 16) | (((g8 * 2) / 3) << 8) | ((r8 * 2) / 3);
                    };

                    for (uint32_t r = 0; r < curRows; ++r)
                    {
                        // Combined index across [scrollback | live]: S - offset + r.
                        int64_t idx = (int64_t)scrollbackCount + (int64_t)r - (int64_t)scrollOffset;
                        bool inLive = idx >= (int64_t)scrollbackCount;
                        uint32_t liveRow = inLive ? (uint32_t)(idx - (int64_t)scrollbackCount) : 0;
                        uint32_t sbLine = inLive ? 0 : (uint32_t)idx;

                        for (uint32_t c = 0; c < curCols; ++c)
                        {
                            terminal_cell tc = inLive ? TerminalScreen::CellAt(*screen, c, liveRow)
                                                      : TerminalScreen::ScrollbackCellAt(*screen, c, sbLine);
                            uint32_t fg = tc.fgRgba;
                            uint32_t bg = tc.bgRgba;
                            if (tc.attrs & TerminalAttr::Bold)
                                fg = brighten(fg);
                            if (tc.attrs & TerminalAttr::Dim)
                                fg = dim(fg);
                            if (tc.attrs & TerminalAttr::Reverse)
                            {
                                uint32_t tmp = fg;
                                fg = bg;
                                bg = tmp;
                            }
                            uint16_t flags = 0;
                            if (tc.attrs & TerminalAttr::Underline)
                                flags |= 1; // CellFlag_Underline
                            if (tc.attrs & TerminalAttr::Strike)
                                flags |= 2; // CellFlag_Strike

                            if (inLive && curVisible && liveRow == curRow && c == curCol)
                            {
                                if (curStyle == terminal_cursor_style::BlinkingBlock ||
                                    curStyle == terminal_cursor_style::SteadyBlock)
                                    flags |= 4;
                                else if (curStyle == terminal_cursor_style::BlinkingUnderline ||
                                         curStyle == terminal_cursor_style::SteadyUnderline)
                                    flags |= 8;
                                else if (curStyle == terminal_cursor_style::BlinkingBar ||
                                         curStyle == terminal_cursor_style::SteadyBar)
                                    flags |= 12;
                            }

                            CellRenderer::PutCell(c, r,
                                                  cell_attr{
                                                      .glyphIndex = CellRenderer::GlyphForCodepoint(tc.codepoint),
                                                      .flags = flags,
                                                      .fgRgba = fg,
                                                      .bgRgba = bg,
                                                  });
                        }
                    }

                    uint64_t paintUs = GetMonotonicTimeMicros() - paintStartUs;
                    if (showStats)
                        DebugTextRenderer::Fmt(500, 50, "paint=%dus"_s, (uint32_t)paintUs);

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

    PlatformPty::Destroy(*pty);
}

} // namespace nyla