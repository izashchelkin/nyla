#include "nyla/commons/profiler.h"

#include <cstdint>

#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/time.h"

namespace nyla
{

namespace
{

constexpr inline uint32_t kMaxEntries = 64;
constexpr inline uint32_t kMaxStack = 8;
constexpr inline uint64_t kNameCap = 32;

struct profile_entry
{
    inline_string<kNameCap> name;
    uint64_t startUs;
    uint64_t durationUs;
    uint8_t depth;
};

struct profiler_state
{
    profile_entry current[kMaxEntries];
    uint32_t currentCount;

    profile_entry display[kMaxEntries];
    uint32_t displayCount;

    uint16_t stack[kMaxStack];
    uint8_t stackDepth;

    uint64_t frameStartUs;
    uint64_t lastFrameUs;
    uint32_t overflowCount;

    bool visible;
};

profiler_state *g_profiler;

void StoreName(inline_string<kNameCap> &dst, byteview name)
{
    uint64_t n = Min<uint64_t>(name.size, kNameCap);
    if (n)
        MemCpy(dst.data.data, name.data, n);
    dst.size = n;
}

} // namespace

namespace Profiler
{

void API Bootstrap()
{
    g_profiler = &RegionAlloc::Alloc<profiler_state>(RegionAlloc::g_BootstrapAlloc);
    g_profiler->currentCount = 0;
    g_profiler->displayCount = 0;
    g_profiler->stackDepth = 0;
    g_profiler->frameStartUs = 0;
    g_profiler->lastFrameUs = 0;
    g_profiler->overflowCount = 0;
    g_profiler->visible = true;
}

void API FrameBegin()
{
    if (!g_profiler)
        return;
    g_profiler->currentCount = 0;
    g_profiler->stackDepth = 0;
    g_profiler->overflowCount = 0;
    g_profiler->frameStartUs = GetMonotonicTimeMicros();
}

void API FrameEnd()
{
    if (!g_profiler)
        return;

    const uint64_t now = GetMonotonicTimeMicros();
    g_profiler->lastFrameUs = now - g_profiler->frameStartUs;

    while (g_profiler->stackDepth > 0)
    {
        const uint16_t idx = g_profiler->stack[--g_profiler->stackDepth];
        auto &e = g_profiler->current[idx];
        e.durationUs = now - e.startUs;
    }

    const uint32_t n = Min<uint32_t>(g_profiler->currentCount, kMaxEntries);
    for (uint32_t i = 0; i < n; ++i)
        g_profiler->display[i] = g_profiler->current[i];
    g_profiler->displayCount = n;
}

void API BeginScope(byteview name)
{
    if (!g_profiler)
        return;
    if (g_profiler->currentCount >= kMaxEntries || g_profiler->stackDepth >= kMaxStack)
    {
        ++g_profiler->overflowCount;
        return;
    }

    const uint16_t idx = (uint16_t)g_profiler->currentCount++;
    auto &e = g_profiler->current[idx];
    StoreName(e.name, name);
    e.startUs = GetMonotonicTimeMicros();
    e.durationUs = 0;
    e.depth = g_profiler->stackDepth;
    g_profiler->stack[g_profiler->stackDepth++] = idx;
}

void API EndScope()
{
    if (!g_profiler)
        return;
    if (g_profiler->stackDepth == 0)
        return;

    const uint16_t idx = g_profiler->stack[--g_profiler->stackDepth];
    auto &e = g_profiler->current[idx];
    e.durationUs = GetMonotonicTimeMicros() - e.startUs;
}

void API ToggleVisible()
{
    if (!g_profiler)
        return;
    g_profiler->visible = !g_profiler->visible;
}

auto API IsVisible() -> bool
{
    return g_profiler && g_profiler->visible;
}

void API CmdFlush(rhi_cmdlist cmd, int32_t originPxX, int32_t originPxY, uint32_t fps)
{
    if (!g_profiler || !g_profiler->visible)
        return;

    constexpr uint32_t kCols = 64;
    constexpr uint32_t kIndent = 2;
    constexpr uint32_t kHeaderRows = 1;

    const uint32_t scopeRows = Min<uint32_t>(g_profiler->displayCount, kMaxEntries);
    const uint32_t rows = kHeaderRows + scopeRows;
    if (!rows)
        return;

    const uint32_t headerFg = 0xFFFFFF80u;
    const uint32_t headerBg = 0xFF202020u;
    const uint32_t rowFg = 0xFFD0D0D0u;
    const uint32_t rowBg = 0xFF202020u;

    CellRenderer::Begin(originPxX, originPxY, kCols, rows);

    const double frameMs = (double)g_profiler->lastFrameUs * 1e-3;
    uint8_t headerBuf[128];
    const uint64_t headerN = StringWriteFmt(span<uint8_t>{headerBuf, sizeof(headerBuf)},
                                            "profiler (F7 hide)  frame %f ms  %u fps"_s, frameMs, fps);
    CellRenderer::Text(0, 0, byteview{headerBuf, headerN}, headerFg, headerBg);

    for (uint32_t i = 0; i < scopeRows; ++i)
    {
        const auto &e = g_profiler->display[i];
        const double ms = (double)e.durationUs * 1e-3;

        uint8_t lineBuf[160];
        uint32_t col = 0;
        for (uint8_t d = 0; d < e.depth && col < (uint32_t)sizeof(lineBuf); ++d)
        {
            for (uint32_t k = 0; k < kIndent && col < (uint32_t)sizeof(lineBuf); ++k)
                lineBuf[col++] = ' ';
        }

        const uint64_t bodyN = StringWriteFmt(span<uint8_t>{lineBuf + col, (uint64_t)sizeof(lineBuf) - col},
                                              "%.*s  %f ms"_s, e.name.size, e.name.data.data, ms);
        CellRenderer::Text(0, i + 1, byteview{lineBuf, col + bodyN}, rowFg, rowBg);
    }

    CellRenderer::CmdFlush(cmd);
}

} // namespace Profiler

} // namespace nyla