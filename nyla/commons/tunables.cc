#include "nyla/commons/tunables.h"

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/stringparser.h"

namespace nyla
{

namespace
{

constexpr inline uint32_t kMaxTunables = 64;
constexpr inline uint64_t kNameCap = 48;
constexpr inline uint64_t kPathCap = 128;

enum class tunable_type : uint8_t
{
    Float,
    Int,
};

struct tunable_entry
{
    inline_string<kNameCap> name;
    void *ptr;
    tunable_type type;
    union {
        struct
        {
            float step;
            float minV;
            float maxV;
        } f;
        struct
        {
            int32_t step;
            int32_t minV;
            int32_t maxV;
        } i;
    };
};

struct pending_value
{
    inline_string<kNameCap> name;
    double value;
};

struct tunables_state
{
    tunable_entry entries[kMaxTunables];
    uint32_t count;
    uint32_t selected;
    bool visible;

    inline_string<kPathCap> persistPath;
    pending_value pending[kMaxTunables];
    uint32_t pendingCount;
};

tunables_state *manager;

auto NameView(const inline_string<kNameCap> &s) -> byteview
{
    return byteview{s.data.data, s.size};
}

auto PathView(const inline_string<kPathCap> &s) -> byteview
{
    return byteview{s.data.data, s.size};
}

auto ReadNameToken(byte_parser &p) -> byteview
{
    const uint8_t *start = p.at;
    while (ByteParser::HasNext(p))
    {
        const uint8_t ch = ByteParser::Peek(p);
        if (ch == ' ' || ch == '\t' || ch == '=' || ch == '\n' || ch == '\r')
            break;
        ByteParser::Advance(p);
    }
    return byteview{start, (uint64_t)(p.at - start)};
}

void ApplyPending(tunable_entry &e, byteview name, void *ptr, tunable_type type)
{
    if (!manager)
        return;
    for (uint32_t i = 0; i < manager->pendingCount; ++i)
    {
        const auto &pv = manager->pending[i];
        if (!Span::Eq(NameView(pv.name), name))
            continue;
        if (type == tunable_type::Float)
        {
            float v = (float)pv.value;
            if (v < e.f.minV)
                v = e.f.minV;
            if (v > e.f.maxV)
                v = e.f.maxV;
            *static_cast<float *>(ptr) = v;
        }
        else
        {
            int32_t v = (int32_t)pv.value;
            if (v < e.i.minV)
                v = e.i.minV;
            if (v > e.i.maxV)
                v = e.i.maxV;
            *static_cast<int32_t *>(ptr) = v;
        }
        return;
    }
}

void LoadFromFile(byteview path)
{
    if (!path.size)
        return;

    file_handle f = FileOpen(path, FileOpenMode::Read);
    if (!FileValid(f))
        return;

    FileSeek(f, 0, file_seek_mode::End);
    const uint64_t size = FileTell(f);
    FileSeek(f, 0, file_seek_mode::Begin);

    constexpr uint64_t kMaxFileBytes = 64 * 1024;
    if (!size || size > kMaxFileBytes)
    {
        FileClose(f);
        return;
    }

    uint8_t buf[kMaxFileBytes];
    uint64_t read = 0;
    uint64_t remaining = size;
    while (remaining > 0)
    {
        uint32_t n = FileRead(f, (uint32_t)remaining, buf + read);
        if (!n)
            break;
        read += n;
        remaining -= n;
    }
    FileClose(f);

    byte_parser p;
    ByteParser::Init(p, buf, read);

    while (ByteParser::HasNext(p))
    {
        StringParser::SkipWhitespace(p);
        if (!ByteParser::HasNext(p))
            break;
        if (ByteParser::Peek(p) == '#')
        {
            ByteParser::NextLine(p);
            continue;
        }

        byteview name = ReadNameToken(p);
        if (!name.size)
        {
            ByteParser::NextLine(p);
            continue;
        }

        while (ByteParser::HasNext(p))
        {
            const uint8_t ch = ByteParser::Peek(p);
            if (ch == '\n' || ch == '\r')
                break;
            if (ch == '=' || ch == ' ' || ch == '\t')
                ByteParser::Advance(p);
            else
                break;
        }

        if (!ByteParser::HasNext(p))
            break;
        const uint8_t ch = ByteParser::Peek(p);
        if (ch == '\n' || ch == '\r')
        {
            ByteParser::NextLine(p);
            continue;
        }

        double dv;
        int64_t lv;
        const auto kind = StringParser::ParseDecimal(p, dv, lv);
        const double value = (kind == StringParser::ParseNumberResult::Double) ? dv : (double)lv;

        if (manager->pendingCount < kMaxTunables)
        {
            auto &pv = manager->pending[manager->pendingCount++];
            InlineString::Assign(pv.name, name);
            pv.value = value;
        }

        ByteParser::NextLine(p);
    }
}

} // namespace

namespace Tunables
{

void API Bootstrap(byteview persistPath)
{
    manager = &RegionAlloc::Alloc<tunables_state>(RegionAlloc::g_BootstrapAlloc);
    manager->count = 0;
    manager->selected = 0;
    manager->visible = false;
    manager->pendingCount = 0;
    InlineString::Assign(manager->persistPath, persistPath);

    LoadFromFile(persistPath);
}

void API RegisterFloat(byteview name, float *ptr, float step, float minV, float maxV)
{
    if (!manager)
        return;
    if (manager->count >= kMaxTunables)
        return;

    auto &e = manager->entries[manager->count++];
    InlineString::Assign(e.name, name);
    e.ptr = ptr;
    e.type = tunable_type::Float;
    e.f.step = step;
    e.f.minV = minV;
    e.f.maxV = maxV;

    ApplyPending(e, name, ptr, tunable_type::Float);
}

void API RegisterInt(byteview name, int32_t *ptr, int32_t step, int32_t minV, int32_t maxV)
{
    if (!manager)
        return;
    if (manager->count >= kMaxTunables)
        return;

    auto &e = manager->entries[manager->count++];
    InlineString::Assign(e.name, name);
    e.ptr = ptr;
    e.type = tunable_type::Int;
    e.i.step = step;
    e.i.minV = minV;
    e.i.maxV = maxV;

    ApplyPending(e, name, ptr, tunable_type::Int);
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

void API SelectPrev()
{
    if (!manager || !manager->count)
        return;
    manager->selected = (manager->selected + manager->count - 1) % manager->count;
}

void API SelectNext()
{
    if (!manager || !manager->count)
        return;
    manager->selected = (manager->selected + 1) % manager->count;
}

void API Decrement()
{
    if (!manager || !manager->count)
        return;

    auto &e = manager->entries[manager->selected];
    if (e.type == tunable_type::Float)
    {
        float &v = *static_cast<float *>(e.ptr);
        v -= e.f.step;
        if (v < e.f.minV)
            v = e.f.minV;
    }
    else
    {
        int32_t &v = *static_cast<int32_t *>(e.ptr);
        v -= e.i.step;
        if (v < e.i.minV)
            v = e.i.minV;
    }
}

void API Increment()
{
    if (!manager || !manager->count)
        return;

    auto &e = manager->entries[manager->selected];
    if (e.type == tunable_type::Float)
    {
        float &v = *static_cast<float *>(e.ptr);
        v += e.f.step;
        if (v > e.f.maxV)
            v = e.f.maxV;
    }
    else
    {
        int32_t &v = *static_cast<int32_t *>(e.ptr);
        v += e.i.step;
        if (v > e.i.maxV)
            v = e.i.maxV;
    }
}

void API Save()
{
    if (!manager || !manager->persistPath.size)
        return;

    file_handle f = FileOpen(PathView(manager->persistPath), FileOpenMode::Write);
    if (!FileValid(f))
    {
        LOG("tunables: save failed (open) %.*s"_s, (int)manager->persistPath.size, manager->persistPath.data.data);
        return;
    }

    uint8_t lineBuf[256];
    for (uint32_t i = 0; i < manager->count; ++i)
    {
        const auto &e = manager->entries[i];
        if (e.type == tunable_type::Float)
        {
            const float v = *static_cast<const float *>(e.ptr);
            FileWriteFmt(f, span<uint8_t>{lineBuf, sizeof(lineBuf)}, "%.*s = %f\n"_s, e.name.size, e.name.data.data,
                         (double)v);
        }
        else
        {
            const int32_t v = *static_cast<const int32_t *>(e.ptr);
            FileWriteFmt(f, span<uint8_t>{lineBuf, sizeof(lineBuf)}, "%.*s = %d\n"_s, e.name.size, e.name.data.data, v);
        }
    }

    FileClose(f);
    LOG("tunables: saved %u to %.*s"_s, manager->count, (int)manager->persistPath.size, manager->persistPath.data.data);
}

void API CmdFlush(rhi_cmdlist cmd, int32_t originPxX, int32_t originPxY)
{
    if (!manager || !manager->visible || !manager->count)
        return;

    constexpr uint32_t kCols = 80;
    const uint32_t rows = manager->count + 1;

    CellRenderer::Begin(originPxX, originPxY, kCols, rows);
    CellRenderer::Text(0, 0, "tunables (F1 hide  F2/F3 sel  F4/F5 -/+  F6 save):"_s, 0xFFFFFF80u, 0xFF202020u);

    for (uint32_t i = 0; i < manager->count; ++i)
    {
        const auto &e = manager->entries[i];
        const bool sel = (i == manager->selected);
        const uint32_t fg = sel ? 0xFF000000u : 0xFFD0D0D0u;
        const uint32_t bg = sel ? 0xFFFFD080u : 0xFF202020u;

        uint8_t lineBuf[160];
        uint64_t n;
        if (e.type == tunable_type::Float)
        {
            const float v = *static_cast<const float *>(e.ptr);
            n = StringWriteFmt(span<uint8_t>{lineBuf, sizeof(lineBuf)}, "%.*s = %f"_s, e.name.size, e.name.data.data,
                               (double)v);
        }
        else
        {
            const int32_t v = *static_cast<const int32_t *>(e.ptr);
            n = StringWriteFmt(span<uint8_t>{lineBuf, sizeof(lineBuf)}, "%.*s = %d"_s, e.name.size, e.name.data.data,
                               v);
        }
        CellRenderer::Text(0, i + 1, byteview{lineBuf, n}, fg, bg);
    }

    CellRenderer::CmdFlush(cmd);
}

} // namespace Tunables

} // namespace nyla
