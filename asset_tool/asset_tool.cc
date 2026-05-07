#include <cinttypes>
#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mem.h"
#include "nyla/commons/pipeline_cache.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/random.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/stringparser.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/tokenparser.h"

#include "nyla/commons/ui_imgui.h"

namespace nyla
{

namespace
{

enum class entry_type : uint8_t
{
    Unknown,
    Texture,
    Spv,
    Bdf,
    Gltf,
    Wav,
    Pipeline,
    Mesh,
};

constexpr byteview EntryTypeName(entry_type t)
{
    switch (t)
    {
    case entry_type::Texture:
        return "tex"_s;
    case entry_type::Spv:
        return "spv"_s;
    case entry_type::Bdf:
        return "bdf"_s;
    case entry_type::Gltf:
        return "gltf"_s;
    case entry_type::Wav:
        return "wav"_s;
    case entry_type::Pipeline:
        return "pipe"_s;
    case entry_type::Mesh:
        return "mesh"_s;
    case entry_type::Unknown:
        return "?"_s;
    }
    return "?"_s;
}

auto SniffType(byteview data, uint64_t guidExpectedTextureSize) -> entry_type
{
    if (data.size >= 4)
    {
        const uint8_t *b = data.data;
        if (b[0] == 0x03 && b[1] == 0x02 && b[2] == 0x23 && b[3] == 0x07)
            return entry_type::Spv;
        if (b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F')
            return entry_type::Wav;
    }
    if (data.size >= 9 && MemStartsWith(data.data, data.size, (const uint8_t *)"STARTFONT", 9))
        return entry_type::Bdf;
    if (data.size >= 1 && data.data[0] == '{')
        return entry_type::Gltf;

    if (data.size >= sizeof(texture_blob_header))
    {
        auto *h = (const texture_blob_header *)data.data;
        if (h->format == 0 && h->pixelOffset >= sizeof(texture_blob_header) && h->pixelOffset <= 64 && h->width > 0 &&
            h->height > 0 && h->width <= 8192 && h->height <= 8192)
        {
            uint64_t needed = (uint64_t)h->pixelOffset + (uint64_t)h->width * h->height * 4;
            if (needed == data.size)
                return entry_type::Texture;
        }
    }

    bool isAscii = true;
    bool hasEquals = false;
    uint64_t scan = data.size < 256 ? data.size : 256;
    for (uint64_t i = 0; i < scan; ++i)
    {
        uint8_t c = data.data[i];
        if (c == '=')
            hasEquals = true;
        if (c == 0 || (c < 0x20 && c != '\n' && c != '\r' && c != '\t'))
        {
            isAscii = false;
            break;
        }
    }
    if (isAscii && hasEquals)
        return entry_type::Pipeline;

    (void)guidExpectedTextureSize;
    return entry_type::Mesh;
}

auto ParseU32Dec(byte_parser &p) -> uint32_t
{
    uint32_t v = 0;
    while (ByteParser::HasNext(p))
    {
        uint8_t c = ByteParser::Peek(p);
        if (c < '0' || c > '9')
            break;
        v = v * 10 + (uint32_t)(c - '0');
        ByteParser::Advance(p);
    }
    return v;
}

auto BdfFontBoundingBox(byteview data, uint32_t &outW, uint32_t &outH) -> bool
{
    byte_parser p;
    ByteParser::Init(p, data.data, data.size);
    while (ByteParser::HasNext(p))
    {
        if (ByteParser::StartsWithAdvance(p, "FONTBOUNDINGBOX "_s))
        {
            uint32_t w = ParseU32Dec(p);
            ASSERT(ByteParser::Read(p) == ' ');
            uint32_t h = ParseU32Dec(p);
            if (w == 0 || h == 0)
                return false;
            outW = w;
            outH = h;
            return true;
        }
        ByteParser::NextLine(p);
    }
    return false;
}

struct entry_row
{
    uint64_t guid;
    uint64_t origOffset; // valid only when staged == false
    uint64_t dataSize;
    entry_type type;
    bool markedDelete;
    bool staged;       // true = pending add not yet in archive on disk
    bool needMintMeta; // true = .meta sidecar absent at stage time; mint on save
    byteview srcPath;  // staged only; copy in persistent alloc
};

struct alias_entry
{
    uint64_t guid;
    byteview alias;
};

struct tool_state
{
    inline_vec<entry_row, 4096> rows;
    inline_vec<alias_entry, 4096> aliases;
    inline_vec<uint32_t, 4096> filtered; // absolute row indices passing filter, rebuilt per frame
    uint32_t scroll;                     // index into filtered, not rows
    uint64_t bdfGuid;                    // first 16x32 BDF found in archive; 0 if none
    uint32_t cellPxW;
    uint32_t cellPxH;
    byteview archivePath;
    ui_state ui;
    uint64_t randomState[4];

    static constexpr uint32_t kFilterCap = 64;
    uint8_t filterBuf[kFilterCap];
    uint32_t filterLen;

    static constexpr uint32_t kAddCap = 256;
    uint8_t addBuf[kAddCap];
    uint32_t addLen;

    bool wantSaveModal;
};
tool_state *tool;

auto MatchesFilter(uint64_t guid, byteview alias) -> bool
{
    if (tool->filterLen == 0)
        return true;
    uint8_t hex[16];
    {
        constexpr const char *kHex = "0123456789abcdef";
        uint64_t v = guid;
        for (int i = 15; i >= 0; --i)
        {
            hex[i] = (uint8_t)kHex[v & 0xF];
            v >>= 4;
        }
    }
    auto haystacks = [&](auto &&body) {
        body(byteview{hex, 16});
        if (alias.size > 0)
            body(alias);
    };
    auto contains = [](byteview hay, byteview needle) -> bool {
        if (needle.size > hay.size)
            return false;
        for (uint64_t i = 0; i + needle.size <= hay.size; ++i)
        {
            bool ok = true;
            for (uint64_t j = 0; j < needle.size; ++j)
            {
                uint8_t a = hay.data[i + j];
                uint8_t b = needle.data[j];
                if (a >= 'A' && a <= 'Z')
                    a += 32;
                if (b >= 'A' && b <= 'Z')
                    b += 32;
                if (a != b)
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return true;
        }
        return false;
    };
    byteview needle{tool->filterBuf, tool->filterLen};
    bool found = false;
    haystacks([&](byteview h) {
        if (!found && contains(h, needle))
            found = true;
    });
    return found;
}

auto LookupAlias(uint64_t guid) -> byteview
{
    for (uint64_t i = 0; i < tool->aliases.size; ++i)
        if (tool->aliases[i].guid == guid)
            return tool->aliases[i].alias;
    return {};
}

void ParseMeta(byteview metaContents, uint64_t &outGuid, byteview &outAlias)
{
    outGuid = 0;
    outAlias = {};
    byte_parser p;
    ByteParser::Init(p, metaContents.data, metaContents.size);
    while (ByteParser::HasNext(p))
    {
        StringParser::SkipWhitespace(p);
        if (!ByteParser::HasNext(p))
            break;
        if (TokenParser::SkipLineComment(p))
            continue;

        byteview key = TokenParser::ParseIdentifier(p);
        TokenParser::SkipLineWhitespace(p);

        if (Span::Eq(key, "guid"_s))
        {
            outGuid = TokenParser::ParseHexU64(p);
        }
        else if (Span::Eq(key, "alias"_s))
        {
            const uint8_t *start = p.at;
            while (ByteParser::HasNext(p) && ByteParser::Peek(p) != '\n' && ByteParser::Peek(p) != '\r')
                ByteParser::Advance(p);
            outAlias = byteview{start, (uint64_t)(p.at - start)};
        }
        ByteParser::NextLine(p);
    }
}

void ScanSidecars(byteview rootDir, region_alloc &persistent, region_alloc &scratch)
{
    dir_iter *it = DirIter::Create(scratch, rootDir);
    if (!it)
        return;

    file_metadata meta;
    while (DirIter::Next(scratch, *it, meta))
    {
        if (Any(meta.attributes & file_attribute::Hidden))
            continue;
        if (Span::Eq(meta.fileName, "."_s) || Span::Eq(meta.fileName, ".."_s))
            continue;

        if (Any(meta.attributes & file_attribute::Directory))
        {
            auto &subDir = RegionAlloc::AllocVec<uint8_t, 0x200>(scratch);
            InlineVec::Append(subDir, rootDir);
            InlineVec::Append(subDir, "/"_s);
            InlineVec::Append(subDir, meta.fileName);
            ScanSidecars(subDir, persistent, scratch);
            continue;
        }

        if (!Span::EndsWith(meta.fileName, ".meta"_s))
            continue;

        auto &metaPath = RegionAlloc::AllocVec<uint8_t, 0x200>(scratch);
        InlineVec::Append(metaPath, rootDir);
        InlineVec::Append(metaPath, "/"_s);
        InlineVec::Append(metaPath, meta.fileName);

        file_handle f = FileOpen(metaPath, FileOpenMode::Read);
        if (!FileValid(f))
            continue;
        span<uint8_t> contents = FileReadFully(scratch, f);
        FileClose(f);

        uint64_t guid = 0;
        byteview alias{};
        ParseMeta(contents, guid, alias);
        if (!guid || alias.size == 0)
            continue;

        InlineVec::Append(tool->aliases, alias_entry{
                                             .guid = guid,
                                             .alias = RegionAlloc::CopyByteView(persistent, alias),
                                         });
    }

    DirIter::Destroy(*it);
}

void LoadIndex(region_alloc &alloc)
{
    file_handle file = FileOpen(tool->archivePath, FileOpenMode::Read);
    ASSERT(FileValid(file), "archive not found");
    span buf = FileReadFully(alloc, file);
    FileClose(file);

    ASSERT(buf.size >= sizeof(assetdb_header));
    auto *header = (const assetdb_header *)buf.data;
    ASSERT(header->magic == kAssetDbMagic);

    auto *index = (const assetdb_index_entry *)(buf.data + sizeof(assetdb_header));
    for (uint32_t i = 0; i < header->entryCount; ++i)
    {
        const assetdb_index_entry &e = index[i];
        byteview data{buf.data + e.dataOffset, e.dataSize};
        entry_type t = SniffType(data, e.dataSize);
        if (t == entry_type::Bdf && tool->bdfGuid == 0)
        {
            uint32_t bbw = 0, bbh = 0;
            if (BdfFontBoundingBox(data, bbw, bbh) && bbw == 16 && bbh == 32)
            {
                tool->bdfGuid = e.guid;
                tool->cellPxW = bbw;
                tool->cellPxH = bbh;
            }
            else
            {
                LOG("asset_tool: skipping BDF 0x%016" PRIx64 " (size %ux%u, need 16x32)", e.guid, bbw, bbh);
            }
        }
        InlineVec::Append(tool->rows, entry_row{
                                          .guid = e.guid,
                                          .origOffset = e.dataOffset,
                                          .dataSize = e.dataSize,
                                          .type = t,
                                      });
    }
}

auto TryReadMetaGuid(byteview srcPath, region_alloc &scratch, bool &outExists) -> uint64_t
{
    auto &mp = RegionAlloc::AllocVec<uint8_t, 0x200>(scratch);
    InlineVec::Append(mp, srcPath);
    InlineVec::Append(mp, ".meta"_s);
    file_handle f = FileOpen(mp, FileOpenMode::Read);
    if (!FileValid(f))
    {
        outExists = false;
        return 0;
    }
    outExists = true;
    span<uint8_t> buf = FileReadFully(scratch, f);
    FileClose(f);
    uint64_t guid = 0;
    byteview alias{};
    ParseMeta(buf, guid, alias);
    return guid;
}

void StageEntry(byteview path, region_alloc &persistent)
{
    region_alloc scratch = RegionAlloc::Create(64_MiB, 0);
    file_handle f = FileOpen(path, FileOpenMode::Read);
    if (!FileValid(f))
    {
        LOG("asset_tool: add: cannot open " SV_FMT, SV_ARG(path));
        RegionAlloc::Destroy(scratch);
        return;
    }
    span<uint8_t> data = FileReadFully(scratch, f);
    FileClose(f);

    bool metaExists = false;
    uint64_t guid = TryReadMetaGuid(path, scratch, metaExists);
    if (guid == 0)
        guid = Xoshiro256ss(tool->randomState);

    entry_type t = SniffType(data, data.size);
    byteview pathCopy = RegionAlloc::CopyByteView(persistent, path);

    InlineVec::Append(tool->rows, entry_row{
                                      .guid = guid,
                                      .origOffset = 0,
                                      .dataSize = data.size,
                                      .type = t,
                                      .markedDelete = false,
                                      .staged = true,
                                      .needMintMeta = !metaExists,
                                      .srcPath = pathCopy,
                                  });
    LOG("asset_tool: staged " SV_FMT " guid=0x%016" PRIx64 " size=%" PRIu64, SV_ARG(path), guid, data.size);

    RegionAlloc::Destroy(scratch);
}

void WriteChunked(file_handle file, const uint8_t *data, uint64_t size)
{
    constexpr uint32_t kChunk = 1u << 20;
    while (size > 0)
    {
        uint32_t n = size > kChunk ? kChunk : (uint32_t)size;
        uint32_t wrote = FileWrite(file, n, data);
        ASSERT(wrote == n);
        data += n;
        size -= n;
    }
}

struct staged_buf
{
    uint32_t rowIdx;
    span<uint8_t> data;
};

void SaveArchive(region_alloc &alloc)
{
    file_handle inFile = FileOpen(tool->archivePath, FileOpenMode::Read);
    ASSERT(FileValid(inFile), "save: input archive missing");
    span<uint8_t> buf = FileReadFully(alloc, inFile);
    FileClose(inFile);

    // Pre-read staged source files so the index header carries authoritative sizes
    // even if the user edited the file between add and save.
    inline_vec<staged_buf, 256> stagedBufs;
    for (uint32_t i = 0; i < tool->rows.size; ++i)
    {
        if (tool->rows[i].markedDelete || !tool->rows[i].staged)
            continue;
        file_handle f = FileOpen(tool->rows[i].srcPath, FileOpenMode::Read);
        if (!FileValid(f))
        {
            LOG("asset_tool: save: cannot reread " SV_FMT ", excluding", SV_ARG(tool->rows[i].srcPath));
            tool->rows[i].markedDelete = true;
            continue;
        }
        span<uint8_t> d = FileReadFully(alloc, f);
        FileClose(f);
        tool->rows[i].dataSize = d.size;
        InlineVec::Append(stagedBufs, staged_buf{i, d});
    }

    uint32_t keptCount = 0;
    for (uint32_t i = 0; i < tool->rows.size; ++i)
        if (!tool->rows[i].markedDelete)
            ++keptCount;

    file_handle outFile = FileOpen(tool->archivePath, FileOpenMode::Write);
    ASSERT(FileValid(outFile), "save: open output failed");

    assetdb_header newHeader{.magic = kAssetDbMagic, .entryCount = keptCount};
    FileWrite(outFile, newHeader);

    uint64_t dataStart = sizeof(assetdb_header) + (uint64_t)keptCount * sizeof(assetdb_index_entry);
    uint64_t cursor = dataStart;
    for (uint32_t i = 0; i < tool->rows.size; ++i)
    {
        const entry_row &r = tool->rows[i];
        if (r.markedDelete)
            continue;
        assetdb_index_entry e{.guid = r.guid, .dataOffset = cursor, .dataSize = r.dataSize};
        FileWrite(outFile, e);
        cursor += r.dataSize;
    }

    uint32_t stagedAt = 0;
    for (uint32_t i = 0; i < tool->rows.size; ++i)
    {
        const entry_row &r = tool->rows[i];
        if (r.markedDelete)
            continue;
        if (r.staged)
        {
            ASSERT(stagedAt < stagedBufs.size && stagedBufs[stagedAt].rowIdx == i);
            const span<uint8_t> &d = stagedBufs[stagedAt].data;
            WriteChunked(outFile, d.data, d.size);
            ++stagedAt;
        }
        else
        {
            WriteChunked(outFile, buf.data + r.origOffset, r.dataSize);
        }
    }

    FileClose(outFile);

    // Mint .meta sidecars for staged entries that didn't have one at stage time.
    for (uint32_t i = 0; i < tool->rows.size; ++i)
    {
        const entry_row &r = tool->rows[i];
        if (r.markedDelete || !r.staged || !r.needMintMeta)
            continue;

        auto &mp = RegionAlloc::AllocVec<uint8_t, 0x200>(alloc);
        InlineVec::Append(mp, r.srcPath);
        InlineVec::Append(mp, ".meta"_s);
        file_handle existing = FileOpen(mp, FileOpenMode::Read);
        if (FileValid(existing))
        {
            FileClose(existing);
            continue;
        }
        file_handle newMeta = FileOpen(mp, FileOpenMode::Write);
        if (!FileValid(newMeta))
        {
            LOG("asset_tool: save: failed to mint meta for " SV_FMT, SV_ARG(r.srcPath));
            continue;
        }
        FileWriteFmt(newMeta, "guid  0x%016" PRIX64 "\n# alias <fill_me>\n"_s, r.guid);
        FileClose(newMeta);
        LOG("asset_tool: minted " SV_FMT ".meta", SV_ARG(r.srcPath));
    }

    // Rebuild in-memory state from new archive on disk.
    tool->rows.size = 0;
    LoadIndex(alloc);
    tool->ui = ui_state{};
    if (tool->scroll >= tool->rows.size)
        tool->scroll = tool->rows.size > 0 ? (uint32_t)tool->rows.size - 1 : 0;

    // Re-scan sidecars so freshly minted .meta files are picked up for display.
    tool->aliases.size = 0;
    {
        region_alloc scratch = RegionAlloc::Create(16_MiB, 0);
        ScanSidecars("asset_public"_s, RegionAlloc::g_BootstrapAlloc, scratch);
        ScanSidecars("assets"_s, RegionAlloc::g_BootstrapAlloc, scratch);
        RegionAlloc::Destroy(scratch);
    }
}

void RenderHex16(uint8_t *out, uint64_t v)
{
    constexpr const char *kHex = "0123456789abcdef";
    for (int i = 15; i >= 0; --i)
    {
        out[i] = (uint8_t)kHex[v & 0xF];
        v >>= 4;
    }
}

constexpr uint32_t kDeleteFg = 0xFFCC4444u;
constexpr uint32_t kStagedFg = 0xFF87CEEBu;
constexpr uint32_t kFilterFg = 0xFFD7D7AFu;
constexpr uint32_t kAddFg = 0xFFAFD7AFu;
constexpr uint32_t kButtonFg = 0xFFCCCCCCu;

void PaintRow(const entry_row &row, int32_t y, uint32_t cols, bool selected)
{
    const ui_theme &th = tool->ui.theme;
    uint32_t fg = selected ? th.bg : (row.markedDelete ? kDeleteFg : (row.staged ? kStagedFg : th.focusedFg));
    uint32_t bg = selected ? th.focusedFg : th.bg;

    uint8_t line[256];
    MemSet(line, ' ', sizeof(line));

    if (row.markedDelete)
        line[16] = 'D';
    else if (row.staged)
        line[16] = 'A';

    uint8_t hex[16];
    RenderHex16(hex, row.guid);
    MemCpy(line, hex, 16);

    uint8_t typeBuf[8];
    byteview tn = EntryTypeName(row.type);
    MemSet(typeBuf, ' ', sizeof(typeBuf));
    MemCpy(typeBuf, tn.data, tn.size);
    MemCpy(line + 18, typeBuf, 5);

    uint8_t sizeBuf[16];
    uint64_t sn = StringWriteFmt(span<uint8_t>{sizeBuf, sizeof(sizeBuf)}, "%10" PRIu64 ""_s, row.dataSize);
    MemCpy(line + 24, sizeBuf, sn);

    byteview alias = LookupAlias(row.guid);
    if (alias.size == 0 && row.staged)
        alias = row.srcPath;
    if (alias.size > 0)
    {
        const uint64_t aliasCol = 36;
        uint64_t copy = alias.size;
        if (aliasCol + copy > sizeof(line))
            copy = sizeof(line) - aliasCol;
        MemCpy(line + aliasCol, alias.data, copy);
    }

    uint64_t printable = cols < sizeof(line) ? cols : sizeof(line);
    CellRenderer::Text(0, (uint32_t)y, byteview{line, printable}, fg, bg);
}

} // namespace

void UserMain()
{
    tool = &RegionAlloc::Alloc<tool_state>(RegionAlloc::g_BootstrapAlloc);
    SeedXoshiro256ss(tool->randomState);

    {
        byteview args[8] = {};
        ParseStdArgs(args, (uint32_t)(sizeof(args) / sizeof(args[0])));
        tool->archivePath = args[1].size > 0 ? args[1] : "assets.bin"_s;
    }

    region_alloc alloc = RegionAlloc::Create(64_MiB, 0);

    Engine::Bootstrap(alloc, engine_init_desc{
                                 .maxFps = 60,
                                 .vsync = true,
                             });

    AssetManager::Bootstrap(FileOpen(tool->archivePath, FileOpenMode::Read));
    GpuUpload::Bootstrap();
    SamplerManager::Bootstrap();
    TextureManager::Bootstrap();
    Shader::Bootstrap();
    PipelineCache::Bootstrap();

    {
        region_alloc loadAlloc = RegionAlloc::Create(64_MiB, 0);
        LoadIndex(loadAlloc);
    }
    ASSERT(tool->bdfGuid != 0, "archive contains no 16x32 BDF asset; asset_tool needs one for its UI font");
    ASSERT(tool->cellPxW == 16 && tool->cellPxH == 32, "atlas geometry expects 16x32 BDF");

    {
        region_alloc scratch = RegionAlloc::Create(16_MiB, 0);
        ScanSidecars("asset_public"_s, RegionAlloc::g_BootstrapAlloc, scratch);
        ScanSidecars("assets"_s, RegionAlloc::g_BootstrapAlloc, scratch);
        RegionAlloc::Destroy(scratch);
        LOG("asset_tool: %" PRIu64 " sidecar aliases", tool->aliases.size);
    }

    CellRenderer::Bootstrap(alloc, cell_renderer_init_desc{
                                       .bdfGuid = tool->bdfGuid,
                                       .cellPxW = tool->cellPxW,
                                       .cellPxH = tool->cellPxH,
                                   });

    Ui::BootstrapInput();
    InputManager::Map(input_id::Custom7, input_interface_type::Keyboard, (uint32_t)KeyPhysical::X);
    InputManager::Map(input_id::Custom8, input_interface_type::Keyboard, (uint32_t)KeyPhysical::S);

    bool prevToggleDelete = false;
    bool prevSave = false;

    render_targets renderTargets{
        .ColorFormat = rhi_texture_format::B8G8R8A8_sRGB,
        .DepthStencilFormat = rhi_texture_format::D32_Float_S8_UINT,
    };

    while (!Engine::ShouldExit())
    {
        engine_frame frame = Engine::FrameBegin(alloc);
        GpuUpload::Update();
        InputManager::Update();
        TextureManager::Update(frame.cmd);

        rhi_texture backbuffer = Rhi::GetTexture(Rhi::GetBackbufferView());
        rhi_texture_info backbufferInfo = Rhi::GetTextureInfo(backbuffer);

        rhi_rtv rtv;
        RenderTargets::GetTargets(renderTargets, backbufferInfo.width, backbufferInfo.height, &rtv, nullptr);

        const uint32_t cellPxW = tool->cellPxW;
        const uint32_t cellPxH = tool->cellPxH;
        const uint32_t cols = backbufferInfo.width > 16 ? (backbufferInfo.width - 16) / cellPxW : 1;
        const uint32_t rows = backbufferInfo.height > 16 ? (backbufferInfo.height - 16) / cellPxH : 1;
        // 1 row title bar + 3 rows toolbar (filter, add, buttons) above list.
        constexpr uint32_t kRowsAboveList = 4;
        const uint32_t listRows = rows > kRowsAboveList ? rows - kRowsAboveList : 1;

        ui_frame_input in = Ui::Pump(tool->ui, frame, (int32_t)listRows);
        // Convert pixel-space pointer to cell coords. CellRenderer::Begin used (8, 8) origin.
        in.pointerX = (frame.pointerX - 8) / (int32_t)cellPxW;
        in.pointerY = (frame.pointerY - 8) / (int32_t)cellPxH;
        in.pointerButtons = frame.pointerButtons;
        in.pointerPress = frame.pointerPress;
        in.pointerRelease = frame.pointerRelease;

        bool toggleDelete = InputManager::IsPressed(input_id::Custom7);
        bool save = InputManager::IsPressed(input_id::Custom8);
        bool toggleDeleteEdge = toggleDelete && !prevToggleDelete;
        bool saveEdge = save && !prevSave;
        prevToggleDelete = toggleDelete;
        prevSave = save;

        tool->filtered.size = 0;
        for (uint32_t i = 0; i < tool->rows.size; ++i)
        {
            byteview alias = LookupAlias(tool->rows[i].guid);
            if (MatchesFilter(tool->rows[i].guid, alias))
                InlineVec::Append(tool->filtered, i);
        }

        CellRenderer::Begin(8, 8, cols, rows);
        Ui::Begin(tool->ui, in, cols, rows);

        constexpr uint32_t kIdMainWin = 9;
        constexpr uint32_t kIdFilter = 1;
        constexpr uint32_t kIdAdd = 2;
        constexpr uint32_t kIdRowsScope = 3;
        constexpr uint32_t kIdSaveBtn = 5;
        constexpr uint32_t kIdDeleteBtn = 6;
        constexpr uint32_t kIdQuitBtn = 7;
        constexpr uint32_t kIdSaveModalWin = 8;
        constexpr uint32_t kSaveModalAccent = 0xFFD7D7AFu;

        ui_text_input_result filterRes{};
        ui_text_input_result addRes{};
        bool saveBtn = false;
        bool deleteBtn = false;
        bool quitBtn = false;
        uint32_t focusedIdx = UINT32_MAX;

        uint8_t titleBuf[64];
        uint64_t titleLen = StringWriteFmt(span<uint8_t>{titleBuf, sizeof(titleBuf)}, "asset_tool  %u/%u entries"_s,
                                           (uint32_t)tool->filtered.size, (uint32_t)tool->rows.size);
        ui_window_desc mainDesc{
            .flags = 0,
            .initialX = 0,
            .initialY = 0,
            .w = (int32_t)cols,
            .h = (int32_t)rows - 1,
            .title = byteview{titleBuf, titleLen},
        };
        if (Ui::BeginWindow(tool->ui, kIdMainWin, mainDesc))
        {
            filterRes = Ui::TextInput(tool->ui, kIdFilter, "filter: "_s, tool->filterBuf, tool_state::kFilterCap,
                                      tool->filterLen, kFilterFg);
            if (filterRes.changed)
                tool->scroll = 0;

            addRes =
                Ui::TextInput(tool->ui, kIdAdd, "add:    "_s, tool->addBuf, tool_state::kAddCap, tool->addLen, kAddFg);

            saveBtn = Ui::Button(tool->ui, kIdSaveBtn, "Save"_s, kButtonFg);
            Ui::SameLine(tool->ui, 1);
            deleteBtn = Ui::Button(tool->ui, kIdDeleteBtn, "Delete"_s, kButtonFg);
            Ui::SameLine(tool->ui, 1);
            quitBtn = Ui::Button(tool->ui, kIdQuitBtn, "Quit"_s, kButtonFg);

            ui_list_desc listDesc{
                .w = (int32_t)cols,
                .visibleRows = (int32_t)listRows,
                .rowCount = (uint32_t)tool->filtered.size,
                .scroll = &tool->scroll,
            };
            ui_list_scope listScope = Ui::BeginList(tool->ui, kIdRowsScope, listDesc);
            for (uint32_t fI = 0; fI < (uint32_t)tool->filtered.size; ++fI)
            {
                uint32_t i = tool->filtered[fI];
                const entry_row &row = tool->rows[i];
                uint32_t localId = (uint32_t)(row.guid ^ (row.guid >> 32));
                ui_list_row_result rr = Ui::ListRow(tool->ui, listScope, fI, localId);
                if (rr.hit.focused)
                    focusedIdx = i;
                if (rr.visible)
                    PaintRow(row, rr.y, cols, rr.hit.focused);
            }
            Ui::EndList(tool->ui, listScope);

            Ui::EndWindow(tool->ui);
        }

        // Handle save trigger here so the modal window paints in the same frame.
        bool modalAlreadyOpen = tool->wantSaveModal;
        bool textMode = Ui::IsTextWidgetFocused(tool->ui);
        if ((!modalAlreadyOpen && !textMode && saveEdge) || saveBtn)
            tool->wantSaveModal = true;

        if (tool->wantSaveModal)
        {
            uint32_t addCount = 0, delCount = 0;
            for (uint32_t i = 0; i < tool->rows.size; ++i)
            {
                if (tool->rows[i].markedDelete)
                    ++delCount;
                if (tool->rows[i].staged && !tool->rows[i].markedDelete)
                    ++addCount;
            }
            uint8_t prompt[160];
            uint64_t promptLen = StringWriteFmt(span<uint8_t>{prompt, sizeof(prompt)},
                                                "Save archive?  +%u staged  -%u deletions"_s, addCount, delCount);
            byteview kHelp = "[Y] confirm  [N / Esc] cancel"_s;
            int32_t innerW = (int32_t)(promptLen > kHelp.size ? promptLen : kHelp.size) + 2;
            ui_window_desc savedDesc{
                .flags = kWindowFlagModal,
                .initialX = (int32_t)cols / 2 - innerW / 2,
                .initialY = (int32_t)rows / 2 - 2,
                .w = innerW,
                .h = 3,
                .title = "Save"_s,
            };
            if (Ui::BeginWindow(tool->ui, kIdSaveModalWin, savedDesc))
            {
                Ui::Text(tool->ui, byteview{prompt, promptLen}, kSaveModalAccent);
                Ui::NewLine(tool->ui);
                Ui::Text(tool->ui, kHelp, kSaveModalAccent);
                Ui::EndWindow(tool->ui);
            }
        }

        Ui::End(tool->ui);

        if (addRes.activated && tool->addLen > 0)
        {
            byteview path{tool->addBuf, tool->addLen};
            StageEntry(path, RegionAlloc::g_BootstrapAlloc);
            tool->addLen = 0;
        }

        if (((!modalAlreadyOpen && !textMode && toggleDeleteEdge) || deleteBtn) && focusedIdx != UINT32_MAX)
            tool->rows[focusedIdx].markedDelete = !tool->rows[focusedIdx].markedDelete;

        if (quitBtn)
            Engine::RequestExit();

        ui_modal_result saveModal = Ui::ModalConfirmCancel(tool->ui);
        if (saveModal.confirm)
        {
            region_alloc saveAlloc = RegionAlloc::Create(64_MiB, 0);
            SaveArchive(saveAlloc);
            RegionAlloc::Destroy(saveAlloc);
            focusedIdx = UINT32_MAX;
            tool->wantSaveModal = false;
        }
        if (saveModal.cancel)
            tool->wantSaveModal = false;

        rhi_texture renderTarget = Rhi::GetTexture(rtv);
        Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::ColorTarget);
        Rhi::PassBegin({.rtv = rtv});
        CellRenderer::CmdFlush(frame.cmd);
        Rhi::PassEnd();

        Rhi::CmdTransitionTexture(frame.cmd, renderTarget, rhi_texture_state::TransferSrc);
        Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::TransferDst);
        Rhi::CmdCopyTexture(frame.cmd, backbuffer, renderTarget);
        Rhi::CmdTransitionTexture(frame.cmd, backbuffer, rhi_texture_state::Present);

        Engine::FrameEnd(alloc);
    }
}

} // namespace nyla
