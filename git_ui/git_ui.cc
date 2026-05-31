#include <cstdint>

#include "assets.h"
#include "nyla/commons/array.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/profiler.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/tunables.h"
#include "nyla/commons/ui_imgui.h"

namespace nyla
{

namespace
{

// ─── Colors ───
constexpr uint32_t kFg = 0xFFCCCCCCu;
constexpr uint32_t kDim = 0xFF888888u;
constexpr uint32_t kModified = 0xFFD7D75Fu;
constexpr uint32_t kAdded = 0xFF5FD75Fu;
constexpr uint32_t kDeleted = 0xFFD75F5Fu;
constexpr uint32_t kUntracked = 0xFF5FD7D7u;
constexpr uint32_t kRenamed = 0xFFD75FD7u;
constexpr uint32_t kHunk = 0xFF87CEEBu;
constexpr uint32_t kTitleBg = 0xFF333333u;
constexpr uint32_t kDiffCtx = 0xFF999999u;
constexpr uint32_t kDiffSep = 0xFF555555u;

enum class dline_t : uint8_t
{
    Header,
    Sep,
    Hunk,
    Ctx,
    Add,
    Del,
    Empty
};

struct diff_line
{
    dline_t type;
    const uint8_t *text;
    uint32_t len;
};

struct file_entry
{
    inline_string<160> path, oldPath;
    uint8_t staged, unstaged;
};

struct app_state
{
    inline_vec<file_entry, 256> files;
    uint32_t selected;
    uint32_t scroll;

    diff_line dlines[512];
    uint32_t dlineCount;
    uint32_t diffScroll;
    uint32_t lastDiffedIdx;

    ui_state ui;

    inline_string<256> repoPath;
    inline_string<128> gitError;
    bool needRefresh;
};
app_state *g_app;

// ─── Git ───

auto Git(region_alloc &al, span<byteview> args, byteview &out) -> int32_t
{
    bool dc = !(g_app->repoPath.size == 1 && g_app->repoPath[0] == '.') && g_app->repoPath.size > 0;
    uint32_t n = 1 + (dc ? 2u : 0u) + (uint32_t)args.size + 1;
    const char **a = (const char **)RegionAlloc::Alloc(al, n * sizeof(const char *), alignof(const char *));
    uint32_t i = 0;
    a[i++] = "git";
    if (dc)
    {
        a[i++] = "-C";
        uint8_t *b = RegionAlloc::Alloc(al, g_app->repoPath.size + 1, 1);
        MemCpy(b, g_app->repoPath.data.data, g_app->repoPath.size);
        b[g_app->repoPath.size] = 0;
        a[i++] = (const char *)b;
    }
    for (uint64_t j = 0; j < args.size; ++j)
    {
        uint8_t *b = RegionAlloc::Alloc(al, args[j].size + 1, 1);
        MemCpy(b, args[j].data, args[j].size);
        b[args[j].size] = 0;
        a[i++] = (const char *)b;
    }
    a[i] = nullptr;
    return RunSync(span<const char *const>{(const char *const *)a, i + 1}, al, out);
}

// ─── Status ───

void ParseStatus(byteview out)
{
    g_app->files.size = 0;
    const uint8_t *p = out.data, *e = out.data + out.size;
    while (p < e)
    {
        if (p + 3 > e)
            break;
        uint8_t x = p[0], y = p[1];
        p += 3;
        if (x == ' ')
            x = 0;
        if (y == ' ')
            y = 0;
        const uint8_t *le = p;
        while (le < e && *le != '\n' && *le != '\r')
            ++le;
        byteview r{p, (uint64_t)(le - p)};
        auto &en = InlineVec::Append(g_app->files);
        MemZero(&en);
        en.staged = x;
        en.unstaged = y;
        bool rn = false;
        for (uint64_t i = 0; i + 4 <= r.size; ++i)
            if (r.data[i] == ' ' && r.data[i + 1] == '-' && r.data[i + 2] == '>' && r.data[i + 3] == ' ')
            {
                InlineString::Assign(en.oldPath, byteview{r.data, i});
                InlineString::Assign(en.path, byteview{r.data + i + 4, r.size - i - 4});
                rn = true;
                break;
            }
        if (!rn)
            InlineString::Assign(en.path, r);
        p = le;
        while (p < e && (*p == '\n' || *p == '\r'))
            ++p;
    }
}

void Refresh(region_alloc &al)
{
    byteview out;
    byteview a[] = {"status"_s, "--porcelain=v1"_s, "-u"_s};
    if (Git(al, span<byteview>{a, 3}, out) == 0)
    {
        ParseStatus(out);
        InlineString::Assign(g_app->gitError, ""_s);
    }
    else
    {
        uint32_t n = Min((uint32_t)out.size, (uint32_t)InlineVec::Capacity(g_app->gitError));
        MemCpy(g_app->gitError.data.data, out.data, n);
        g_app->gitError.size = n;
    }
    g_app->needRefresh = false;
    if (g_app->selected != UINT32_MAX && g_app->selected >= g_app->files.size)
        g_app->selected = g_app->files.size > 0 ? (uint32_t)(g_app->files.size - 1) : UINT32_MAX;
    g_app->lastDiffedIdx = UINT32_MAX;
    g_app->dlineCount = 0;
}

// ─── Actions ───

void StageFile(region_alloc &al, uint32_t i)
{
    if (i >= g_app->files.size || g_app->files[i].unstaged == 0)
        return;
    byteview o;
    byteview a[] = {"add"_s, "--"_s, g_app->files[i].path};
    Git(al, span<byteview>{a, 3}, o);
    g_app->needRefresh = true;
}
void StageAll(region_alloc &al)
{
    byteview o;
    byteview a[] = {"add"_s, "-A"_s};
    Git(al, span<byteview>{a, 2}, o);
    g_app->needRefresh = true;
}
void UnstageFile(region_alloc &al, uint32_t i)
{
    if (i >= g_app->files.size || g_app->files[i].staged == 0)
        return;
    byteview o;
    byteview a[] = {"reset"_s, "HEAD"_s, "--"_s, g_app->files[i].path};
    Git(al, span<byteview>{a, 4}, o);
    g_app->needRefresh = true;
}
void UnstageAll(region_alloc &al)
{
    byteview o;
    byteview a[] = {"reset"_s, "HEAD"_s};
    Git(al, span<byteview>{a, 2}, o);
    g_app->needRefresh = true;
}
void RevertFile(region_alloc &al, uint32_t i)
{
    if (i >= g_app->files.size || g_app->files[i].unstaged == 0)
        return;
    byteview o;
    if (g_app->files[i].unstaged == '?')
    {
        byteview a[] = {"clean"_s, "-f"_s, "--"_s, g_app->files[i].path};
        Git(al, span<byteview>{a, 4}, o);
    }
    else
    {
        byteview a[] = {"checkout"_s, "--"_s, g_app->files[i].path};
        Git(al, span<byteview>{a, 3}, o);
    }
    g_app->needRefresh = true;
}

// ─── Diff parsing ───

void ParseDiff(byteview raw, uint8_t *buf, uint32_t cap)
{
    g_app->dlineCount = 0;
    if (raw.size == 0)
        return;
    const uint8_t *p = raw.data, *e = raw.data + raw.size;
    uint32_t adds = 0, dels = 0;
    byteview fn{};
    bool inH = false;
    auto emit = [&](dline_t t, const uint8_t *s, uint32_t n) {
        if (g_app->dlineCount >= 510)
            return;
        uint32_t c = Min(n, cap);
        MemCpy(buf, s, c);
        g_app->dlines[2 + g_app->dlineCount++] = diff_line{t, buf, c};
        buf += c;
        cap -= c;
    };
    while (p < e && g_app->dlineCount < 510)
    {
        const uint8_t *le = p;
        while (le < e && *le != '\n' && *le != '\r')
            ++le;
        uint32_t ll = (uint32_t)(le - p);
        if (ll >= 2 && p[0] == '@' && p[1] == '@')
        {
            inH = true;
            emit(dline_t::Hunk, p, ll);
        }
        else if (!inH && ll >= 12 && MemStartsWith(p, ll, (const uint8_t *)"diff --git ", 11))
        {
            for (uint32_t j = 2; j + 4 <= ll; ++j)
                if (p[j] == ' ' && p[j + 1] == 'b' && p[j + 2] == '/' && p[j - 2] == 'a' && p[j - 1] == '/')
                {
                    fn = byteview{p + j - 1, (uint64_t)(j - 2)};
                    break;
                }
        }
        else if (inH)
        {
            if (ll >= 1 && p[0] == '+' && !(ll >= 3 && p[1] == '+' && p[2] == '+'))
            {
                ++adds;
                emit(dline_t::Add, p, ll);
            }
            else if (ll >= 1 && p[0] == '-' && !(ll >= 3 && p[1] == '-' && p[2] == '-'))
            {
                ++dels;
                emit(dline_t::Del, p, ll);
            }
            else if (ll >= 1 && p[0] == ' ')
                emit(dline_t::Ctx, p, ll);
            else if (ll == 0)
                emit(dline_t::Empty, nullptr, 0);
        }
        p = le + 1;
        if (p > e)
            p = e;
    }
    {
        uint8_t h[128];
        uint64_t hl;
        if (fn.size > 0)
            hl = StringWriteFmt(span<uint8_t>{h, sizeof(h)}, "+%u -%u  " SV_FMT ""_s, adds, dels, SV_ARG(fn));
        else
            hl = StringWriteFmt(span<uint8_t>{h, sizeof(h)}, "+%u -%u"_s, adds, dels);
        uint32_t n = Min((uint32_t)hl, cap);
        MemCpy(buf, h, n);
        g_app->dlines[0] = {dline_t::Header, buf, n};
        buf += n;
        cap -= n;
    }
    {
        uint8_t s[64];
        uint32_t sw = Min(64u, Min(cap, 60u));
        MemSet(s, 0xC4, sw);
        MemCpy(buf, s, sw);
        g_app->dlines[1] = {dline_t::Sep, buf, sw};
    }
    g_app->dlineCount += 2;
}

void FetchDiff(region_alloc &al, uint32_t i)
{
    if (i >= g_app->files.size)
        return;
    if (i == g_app->lastDiffedIdx && g_app->dlineCount > 0)
        return;
    auto &f = g_app->files[i];
    g_app->dlineCount = 0;
    g_app->lastDiffedIdx = i;
    g_app->diffScroll = 0;
    byteview o;
    if (f.unstaged != 0 && f.unstaged != '?')
    {
        byteview a[] = {"diff"_s, "--"_s, f.path};
        Git(al, span<byteview>{a, 3}, o);
    }
    else if (f.staged != 0 && f.staged != 'D')
    {
        byteview a[] = {"diff"_s, "--cached"_s, "--"_s, f.path};
        Git(al, span<byteview>{a, 4}, o);
    }
    else if (f.staged == 'D')
    {
        byteview a[] = {"diff"_s, "--cached"_s, "--"_s, f.path};
        Git(al, span<byteview>{a, 4}, o);
    }
    else if (f.unstaged == '?')
    {
        byteview a[] = {"diff"_s, "--no-index"_s, "/dev/null"_s, f.path};
        Git(al, span<byteview>{a, 4}, o);
    }
    static uint8_t scr[0x2000];
    ParseDiff(o, scr, sizeof(scr));
}

// ─── Helpers ───

auto StColor(uint8_t s) -> uint32_t
{
    switch (s)
    {
    case 'M':
        return kModified;
    case 'A':
        return kAdded;
    case 'D':
        return kDeleted;
    case 'R':
    case 'C':
        return kRenamed;
    case '?':
        return kUntracked;
    default:
        return kDim;
    }
}
auto StChar(uint8_t s) -> uint8_t
{
    return s ? s : ' ';
}
auto DiffFg(const diff_line &dl) -> uint32_t
{
    switch (dl.type)
    {
    case dline_t::Header:
        return kModified;
    case dline_t::Sep:
        return kDiffSep;
    case dline_t::Hunk:
        return kHunk;
    case dline_t::Add:
        return kAdded;
    case dline_t::Del:
        return kDeleted;
    case dline_t::Ctx:
        return kDiffCtx;
    case dline_t::Empty:
        return kDim;
    }
    return kFg;
}

} // namespace

void UserMain()
{
    g_app = &RegionAlloc::Alloc<app_state>(RegionAlloc::g_BootstrapAlloc);
    g_app->selected = UINT32_MAX;
    g_app->scroll = 0;
    g_app->dlineCount = 0;
    g_app->diffScroll = 0;
    g_app->lastDiffedIdx = UINT32_MAX;
    g_app->needRefresh = true;
    {
        byteview a[8] = {};
        ParseStdArgs(a, 8);
        InlineString::Assign(g_app->repoPath, a[1].size > 0 ? a[1] : "."_s);
    }

    region_alloc al = RegionAlloc::Create(64_MiB, 0);
    Engine::Bootstrap(al, engine_init_desc{.maxFps = 60, .vsync = true});
    AssetManager::Bootstrap(FileOpen("assets.bin"_s, FileOpenMode::Read));
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    Shader::Bootstrap();
    PipelineCache::Bootstrap();
    CellRenderer::Bootstrap(al, cell_renderer_init_desc{.bdfGuid = ID_bdf_terminus_u32});
    Profiler::Bootstrap();
#if !defined(NDEBUG)
    Tunables::Bootstrap("git_ui.tunables"_s);
#endif
    Ui::BootstrapInput();
    InputManager::Map(input_id::Custom7, input_interface_type::Keyboard, (uint32_t)KeyPhysical::S);
    InputManager::Map(input_id::Custom8, input_interface_type::Keyboard, (uint32_t)KeyPhysical::U);
    InputManager::Map(input_id::Custom9, input_interface_type::Keyboard, (uint32_t)KeyPhysical::A);
    InputManager::Map(input_id::Custom10, input_interface_type::Keyboard, (uint32_t)KeyPhysical::Z);
    InputManager::Map(input_id::Custom11, input_interface_type::Keyboard, (uint32_t)KeyPhysical::R);
    InputManager::Map(input_id::Custom12, input_interface_type::Keyboard, (uint32_t)KeyPhysical::D);
    InputManager::Map(input_id::Custom13, input_interface_type::Keyboard, (uint32_t)KeyPhysical::F5);
    InputManager::Map(input_id::Custom14, input_interface_type::Keyboard, (uint32_t)KeyPhysical::Q);

    render_targets rts{.ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
                       .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT};
    uint32_t pk[8] = {};

    while (!Engine::ShouldExit())
    {
        engine_frame fr = Engine::FrameBegin(al);
        GpuUpload::Update();
        InputManager::Update();
        TextureManager::Update(fr.cmd);
        if (g_app->needRefresh)
            Refresh(al);
        if (g_app->selected != UINT32_MAX && g_app->selected != g_app->lastDiffedIdx)
            FetchDiff(al, g_app->selected);

        rhi_texture bb = Rhi::GetTexture(Rhi::GetBackbufferView());
        rhi_texture_info bi = Rhi::GetTextureInfo(bb);
        rhi_rtv rtv;
        RenderTargets::GetTargets(rts, bi.width, bi.height, &rtv, nullptr);

        constexpr int32_t Ox = 8, Oy = 8;
        const uint32_t cols = bi.width > (uint32_t)(2 * Ox) ? (bi.width - 2 * Ox) / 16 : 80;
        const uint32_t rows = bi.height > (uint32_t)(2 * Oy) ? (bi.height - 2 * Oy) / 32 : 24;
        int32_t half = (int32_t)cols / 2, leftW = Max(half, 20), rightW = Max((int32_t)cols - leftW - 1, 0);
        int32_t bodyRows = Max((int32_t)rows - 1, 1), listRows = Max(bodyRows - 2, 1); // 2 rows for button bar
        int32_t ptrX = (fr.pointerX - Ox) / 16, ptrY = (fr.pointerY - Oy) / 32;

        bool kS = InputManager::IsPressed(input_id::Custom7), kU = InputManager::IsPressed(input_id::Custom8);
        bool kA = InputManager::IsPressed(input_id::Custom9), kZ = InputManager::IsPressed(input_id::Custom10);
        bool kR = InputManager::IsPressed(input_id::Custom11), kD = InputManager::IsPressed(input_id::Custom12);
        bool kF5 = InputManager::IsPressed(input_id::Custom13), kQ = InputManager::IsPressed(input_id::Custom14);
        bool se = kS && !pk[0], ue = kU && !pk[1], ae = kA && !pk[2], ze = kZ && !pk[3], re = kR && !pk[4],
             de = kD && !pk[5], fe = kF5 && !pk[6], qe = kQ && !pk[7];
        pk[0] = kS;
        pk[1] = kU;
        pk[2] = kA;
        pk[3] = kZ;
        pk[4] = kR;
        pk[5] = kD;
        pk[6] = kF5;
        pk[7] = kQ;

        ui_frame_input in = Ui::Pump(g_app->ui, fr, listRows);
        in.pointerX = ptrX;
        in.pointerY = ptrY;
        in.pointerButtons = fr.pointerButtons;
        in.pointerPress = fr.pointerPress;
        in.pointerRelease = fr.pointerRelease;
        CellRenderer::Begin(Ox, Oy, cols, rows);
        Ui::Begin(g_app->ui, in, cols, rows);

        // ── Title ──
        {
            uint8_t t[256];
            uint32_t sc = 0, uc = 0;
            for (uint32_t i = 0; i < g_app->files.size; ++i)
            {
                if (g_app->files[i].staged)
                    ++sc;
                if (g_app->files[i].unstaged)
                    ++uc;
            }
            uint64_t tl;
            byteview r{g_app->repoPath.data.data, g_app->repoPath.size};
            if (g_app->gitError.size > 0)
            {
                byteview e{g_app->gitError.data.data, g_app->gitError.size};
                tl = StringWriteFmt(span<uint8_t>{t, sizeof(t)}, "git_ui  err: " SV_FMT ""_s, SV_ARG(e));
            }
            else
                tl = StringWriteFmt(span<uint8_t>{t, sizeof(t)}, "git_ui  %uS/%uU/%uT  " SV_FMT ""_s, sc, uc,
                                    (uint32_t)g_app->files.size, SV_ARG(r));
            CellRenderer::Text(0, 0, byteview{t, tl}, kFg, kTitleBg);
        }

        bool sb = false, ub = false, ab = false, zb = false, rb = false, db = false, fb = false, qb = false;

        // ── LEFT: Buttons + File list ──
        {
            ui_window_desc ld{.flags = kWindowFlagReposition,
                              .initialX = 0,
                              .initialY = 1,
                              .w = leftW,
                              .h = bodyRows,
                              .title = "Changes"_s};
            if (Ui::BeginWindow(g_app->ui, 1, ld))
            {
                int32_t bx, by;
                Ui::GetCursor(g_app->ui, bx, by);

                // Button bar: two rows. Row 0 = common actions, Row 1 = less common.
                auto btn = [&](uint32_t id, byteview label) -> bool {
                    return Ui::Button(g_app->ui, id, label, kFg, kButtonFlagNoFocus);
                };

                sb = btn(10, "[S] Stage"_s);
                Ui::SameLine(g_app->ui, 1);
                ub = btn(11, "[U] Unstage"_s);
                Ui::SameLine(g_app->ui, 1);
                ab = btn(12, "[A] Stage all"_s);
                Ui::SameLine(g_app->ui, 1);
                zb = btn(13, "[Z] Unstage all"_s);
                Ui::SameLine(g_app->ui, 1);
                rb = btn(14, "[R] Revert"_s);
                Ui::SameLine(g_app->ui, 1);
                db = btn(15, "[D] Refresh diff"_s);

                Ui::NewLine(g_app->ui);
                Ui::SetCursor(g_app->ui, bx, by + 1);
                fb = btn(16, "[F5] Refresh"_s);
                Ui::SameLine(g_app->ui, 1);
                qb = btn(17, "[Q] Quit"_s);
                Ui::SameLine(g_app->ui, 1);
                // Help text on row 1
                Ui::Text(g_app->ui, "   Enter = toggle stage/unstage   Arrows = navigate"_s, kDim);

                // File list below buttons
                Ui::SetCursor(g_app->ui, bx, by + 2);
                {
                    ui_list_desc ld2{.w = leftW,
                                     .visibleRows = listRows,
                                     .rowCount = (uint32_t)g_app->files.size,
                                     .scroll = &g_app->scroll};
                    ui_list_scope ls = Ui::BeginList(g_app->ui, 2, ld2);
                    uint32_t fi = UINT32_MAX;
                    for (uint32_t i = 0; i < g_app->files.size; ++i)
                    {
                        ui_list_row_result rr = Ui::ListRow(g_app->ui, ls, i, i + 1);
                        if (rr.hit.focused)
                            fi = i;
                        if (rr.hit.activated)
                        {
                            if (g_app->files[i].unstaged)
                                StageFile(al, i);
                            else if (g_app->files[i].staged)
                                UnstageFile(al, i);
                        }
                        if (rr.visible)
                        {
                            const file_entry &f = g_app->files[i];
                            uint8_t line[256];
                            MemSet(line, ' ', sizeof(line));
                            line[0] = StChar(f.staged);
                            line[1] = StChar(f.unstaged);
                            line[2] = ' ';
                            uint64_t pos = 3;
                            if (f.oldPath.size > 0)
                            {
                                uint64_t n = Min(f.oldPath.size, (uint64_t)sizeof(line) - pos);
                                MemCpy(line + pos, f.oldPath.data.data, n);
                                pos += n;
                                if (pos + 4 <= sizeof(line))
                                {
                                    MemCpy(line + pos, " -> ", 4);
                                    pos += 4;
                                }
                            }
                            uint64_t n = Min(f.path.size, (uint64_t)sizeof(line) - pos);
                            MemCpy(line + pos, f.path.data.data, n);
                            pos += n;
                            uint32_t fg, bg;
                            if (rr.hit.focused)
                            {
                                bg = f.staged ? StColor(f.staged)
                                              : (f.unstaged ? StColor(f.unstaged) : g_app->ui.theme.focusedFg);
                                fg = g_app->ui.theme.bg;
                            }
                            else
                            {
                                fg = f.staged ? StColor(f.staged) : f.unstaged ? StColor(f.unstaged) : kFg;
                                bg = g_app->ui.theme.bg;
                            }
                            CellRenderer::Text(0, (uint32_t)rr.y, byteview{line, Min(pos, (uint64_t)leftW)}, fg, bg);
                        }
                    }
                    Ui::EndList(g_app->ui, ls);
                    g_app->selected = fi;
                }
                Ui::EndWindow(g_app->ui);
            }
        }

        // ── RIGHT: Diff ──
        {
            ui_window_desc rd{.flags = kWindowFlagReposition,
                              .initialX = leftW + 1,
                              .initialY = 1,
                              .w = rightW,
                              .h = bodyRows,
                              .title = "Diff"_s};
            if (Ui::BeginWindow(g_app->ui, 3, rd))
            {
                if (g_app->selected == UINT32_MAX || g_app->selected >= g_app->files.size)
                {
                    // No file selected
                    Ui::Text(g_app->ui, "Select a file to view diff"_s, kDim);
                    Ui::NewLine(g_app->ui);
                    Ui::Text(g_app->ui, "Use arrow keys to navigate, Enter to toggle stage"_s, kDim);
                }
                else if (g_app->dlineCount == 0)
                {
                    Ui::Text(g_app->ui, "Loading diff..."_s, kDim);
                }
                else
                {
                    // Diff header line
                    file_entry &f = g_app->files[g_app->selected];
                    uint8_t hdr[256];
                    byteview pv{f.path.data.data, f.path.size};
                    uint64_t hl =
                        StringWriteFmt(span<uint8_t>{hdr, sizeof(hdr)}, "%c%c " SV_FMT "  [%u/%u]"_s, StChar(f.staged),
                                       StChar(f.unstaged), SV_ARG(pv), g_app->diffScroll + 1, g_app->dlineCount);
                    Ui::Text(g_app->ui, byteview{hdr, hl}, kModified);

                    uint32_t contentH = (uint32_t)Max(bodyRows - 1, 1); // one row for the header
                    Ui::ui_child_desc cd{.w = rightW,
                                         .h = (int32_t)contentH,
                                         .flags = Ui::kChildFlagScrollable | Ui::kChildFlagBorder,
                                         .scrollY = &g_app->diffScroll,
                                         .contentRows = g_app->dlineCount};
                    if (Ui::BeginChild(g_app->ui, 4, cd))
                    {
                        int32_t cx, cy;
                        Ui::GetCursor(g_app->ui, cx, cy);
                        uint32_t fst = g_app->diffScroll, lst = Min(fst + contentH, g_app->dlineCount);
                        for (uint32_t i = fst; i < lst; ++i)
                        {
                            const diff_line &dl = g_app->dlines[i];
                            uint32_t fg = DiffFg(dl);
                            if (dl.type == dline_t::Empty)
                                CellRenderer::Text((uint32_t)cx, (uint32_t)(cy + (i - fst)), " "_s, fg,
                                                   g_app->ui.theme.bg);
                            else
                                CellRenderer::Text((uint32_t)cx, (uint32_t)(cy + (i - fst)),
                                                   byteview{dl.text, Min((uint64_t)dl.len, (uint64_t)rightW - 2)}, fg,
                                                   g_app->ui.theme.bg);
                        }
                        Ui::EndChild(g_app->ui);
                    }
                }
                Ui::EndWindow(g_app->ui);
            }
        }

        Ui::End(g_app->ui);

        // ── Dispatch ──
        if (!Ui::IsTextWidgetFocused(g_app->ui))
        {
            if ((sb || se) && g_app->selected != UINT32_MAX)
                StageFile(al, g_app->selected);
            if ((ub || ue) && g_app->selected != UINT32_MAX)
                UnstageFile(al, g_app->selected);
            if (ab || ae)
                StageAll(al);
            if (zb || ze)
                UnstageAll(al);
            if ((rb || re) && g_app->selected != UINT32_MAX)
                RevertFile(al, g_app->selected);
            if (db || de)
            {
                if (g_app->selected != UINT32_MAX)
                {
                    g_app->lastDiffedIdx = UINT32_MAX;
                    FetchDiff(al, g_app->selected);
                }
            }
            if (fb || fe)
                g_app->needRefresh = true;
            if (qb || qe)
                Engine::RequestExit();
        }

        rhi_texture rt = Rhi::GetTexture(rtv);
        Rhi::CmdTransitionTexture(fr.cmd, rt, rhi_texture_state::ColorTarget);
        Rhi::PassBegin({.rtv = rtv});
        CellRenderer::CmdFlush(fr.cmd);
        Rhi::PassEnd();
        Rhi::CmdTransitionTexture(fr.cmd, rt, rhi_texture_state::TransferSrc);
        Rhi::CmdTransitionTexture(fr.cmd, bb, rhi_texture_state::TransferDst);
        Rhi::CmdCopyTexture(fr.cmd, bb, rt);
        Rhi::CmdTransitionTexture(fr.cmd, bb, rhi_texture_state::Present);
        Engine::FrameEnd(al);
    }
}

} // namespace nyla
