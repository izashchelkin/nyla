#include "nyla/commons/profiler.h"

#include <cstdint>

#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/macros.h"
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

profiler_state *manager;

} // namespace

namespace Profiler
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<profiler_state>(RegionAlloc::g_BootstrapAlloc);
    manager->currentCount = 0;
    manager->displayCount = 0;
    manager->stackDepth = 0;
    manager->frameStartUs = 0;
    manager->lastFrameUs = 0;
    manager->overflowCount = 0;
    manager->visible = true;
}

void API FrameBegin()
{
    if (!manager)
        return;
    manager->currentCount = 0;
    manager->stackDepth = 0;
    manager->overflowCount = 0;
    manager->frameStartUs = GetMonotonicTimeMicros();
}

void API FrameEnd()
{
    if (!manager)
        return;

    const uint64_t now = GetMonotonicTimeMicros();
    manager->lastFrameUs = now - manager->frameStartUs;

    while (manager->stackDepth > 0)
    {
        const uint16_t idx = manager->stack[--manager->stackDepth];
        auto &e = manager->current[idx];
        e.durationUs = now - e.startUs;
    }

    const uint32_t n = Min<uint32_t>(manager->currentCount, kMaxEntries);
    for (uint32_t i = 0; i < n; ++i)
        manager->display[i] = manager->current[i];
    manager->displayCount = n;
}

void API BeginScope(byteview name)
{
    if (!manager)
        return;
    if (manager->currentCount >= kMaxEntries || manager->stackDepth >= kMaxStack)
    {
        ++manager->overflowCount;
        return;
    }

    const uint16_t idx = (uint16_t)manager->currentCount++;
    auto &e = manager->current[idx];
    InlineString::Assign(e.name, name);
    e.startUs = GetMonotonicTimeMicros();
    e.durationUs = 0;
    e.depth = manager->stackDepth;
    manager->stack[manager->stackDepth++] = idx;
}

void API EndScope()
{
    if (!manager)
        return;
    if (manager->stackDepth == 0)
        return;

    const uint16_t idx = manager->stack[--manager->stackDepth];
    auto &e = manager->current[idx];
    e.durationUs = GetMonotonicTimeMicros() - e.startUs;
}

void API ToggleVisible()
{
    if (!manager)
        return;
    manager->visible = !manager->visible;
}

auto API IsVisible() -> bool
{
    return manager && manager->visible;
}

void API CmdFlush(rhi_cmdlist cmd, int32_t originPxX, int32_t originPxY, uint32_t fps)
{
    if (!manager || !manager->visible)
        return;

    constexpr uint32_t kCols = 64;
    constexpr uint32_t kIndent = 2;
    constexpr uint32_t kHeaderRows = 1;

    const uint32_t scopeRows = Min<uint32_t>(manager->displayCount, kMaxEntries);
    const uint32_t rows = kHeaderRows + scopeRows;
    if (!rows)
        return;

    const uint32_t headerFg = 0xFFFFFF80u;
    const uint32_t headerBg = 0xFF202020u;
    const uint32_t rowFg = 0xFFD0D0D0u;
    const uint32_t rowBg = 0xFF202020u;

    CellRenderer::Begin(originPxX, originPxY, kCols, rows);

    const double frameMs = (double)manager->lastFrameUs * 1e-3;
    uint8_t headerBuf[128];
    const uint64_t headerN = StringWriteFmt(span<uint8_t>{headerBuf, sizeof(headerBuf)},
                                            "profiler (F7 hide)  frame %f ms  %u fps"_s, frameMs, fps);
    CellRenderer::Text(0, 0, byteview{headerBuf, headerN}, headerFg, headerBg);

    for (uint32_t i = 0; i < scopeRows; ++i)
    {
        const auto &e = manager->display[i];
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
