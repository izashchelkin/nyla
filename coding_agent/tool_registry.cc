// ─── Tool registry — dispatch and all tool handler implementations ───────

#include "coding_agent/tool_registry.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"
#include "nyla/commons/subprocess.h"

#include "xcav/backup.h"
#include "xcav/editor.h"
#include "xcav/insert.h"

namespace nyla
{

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

// Allocate a region-backed copy of a byteview.
static auto AllocCopy(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(buf.data, src.data, src.size);
    return {buf.data, src.size};
}

// Allocate a region-backed copy of a null-terminated C string.
static auto AllocCStr(region_alloc &alloc, const char *src) -> byteview
{
    uint32_t len = 0;
    while (src[len])
        ++len;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, len);
    MemCpy(buf.data, src, len);
    return {buf.data, len};
}

// Convert a C string literal to byteview (no allocation).
static auto Bv(const char *s) -> byteview
{
    uint32_t len = 0;
    while (s[len])
        ++len;
    return {(const uint8_t *)s, len};
}

// Make a result from a stack-allocated formatted string.
static auto MakeResult(region_alloc &alloc, bool isError, const uint8_t *data, uint32_t size) -> ToolResult
{
    ToolResult r;
    r.isError = isError;
    r.content = AllocCopy(alloc, {data, size});
    return r;
}

// Format a result using a fmt string + args into a stack buffer, then copy to region.
#define FMT_RESULT(isErr, fmtStr, ...)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        uint8_t _buf[4096];                                                                                            \
        uint32_t _len = StringWriteFmt(span<uint8_t>{_buf, sizeof(_buf) - 1}, fmtStr, ##__VA_ARGS__);                  \
        _buf[_len] = 0;                                                                                                \
        return MakeResult(alloc, isErr, _buf, _len);                                                                   \
    } while (0)

// ─── JSON argument parsing ─────────────────────────────────────────────────

static auto ParseArgs(byteview argsJson, region_alloc &alloc) -> json_value *
{
    if (argsJson.size == 0)
        return nullptr;
    span<json_value> storage = RegionAlloc::AllocArray<json_value>(alloc, 64);
    json_parser parser;
    JsonParser::Init(parser, argsJson, storage);
    return JsonParser::ParseNext(parser);
}

static auto ArgString(json_value *args, const char *key, byteview def = {}) -> byteview
{
    if (!args || args->tag != json_tag::ObjectBegin)
        return def;
    byteview result;
    if (JsonValue::TryString(*args, Bv(key), result))
        return result;
    return def;
}

static auto ArgU32(json_value *args, const char *key, uint32_t def = 0) -> uint32_t
{
    if (!args || args->tag != json_tag::ObjectBegin)
        return def;
    uint32_t result;
    if (JsonValue::TryDWord(*args, Bv(key), result))
        return result;
    return def;
}

static auto ArgBool(json_value *args, const char *key, bool def = false) -> bool
{
    if (!args || args->tag != json_tag::ObjectBegin)
        return def;
    bool result;
    if (JsonValue::TryBool(*args, Bv(key), result))
        return result;
    return def;
}

// ─── Path helpers ──────────────────────────────────────────────────────────

// Ensure a path is null-terminated for FileOpen (which uses Span::CStr internally).
static auto NullTermPath(region_alloc &alloc, byteview path) -> byteview
{
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, path.size + 1);
    MemCpy(buf.data, path.data, path.size);
    buf.data[path.size] = 0;
    return {buf.data, path.size};
}

// ─── Temp file helpers ─────────────────────────────────────────────────────

static uint32_t s_tempCounter = 0;

static auto WriteTempFile(region_alloc &alloc, byteview content, const char *suffix) -> byteview
{
    uint32_t pid = GetProcessId();
    uint32_t n = s_tempCounter++;

    uint8_t pathBuf[256];
    uint32_t pathLen =
        StringWriteFmt(span<uint8_t>{pathBuf, sizeof(pathBuf) - 1}, "/tmp/ca_%u_%u_%s"_s, pid, n, suffix);
    pathBuf[pathLen] = 0;

    file_handle fh = FileOpen({pathBuf, pathLen}, FileOpenMode::Write);
    if (!FileValid(fh))
        return {};

    FileWrite(fh, content.size, content.data);
    FileClose(fh);

    return AllocCopy(alloc, {pathBuf, pathLen});
}

// ─── Block list formatting ─────────────────────────────────────────────────

static auto FmtBlockList(region_alloc &alloc, byteview filePath, inline_vec<block_info, 256> const &blocks) -> byteview
{
    uint8_t buf[8192];
    uint32_t pos = 0;

    pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "%.*s (%llu blocks)\n"_s, (int)filePath.size,
                          filePath.data, (unsigned long long)blocks.size);

    for (uint64_t i = 0; i < blocks.size; ++i)
    {
        if (pos + 256 > sizeof(buf))
            break;
        block_info const &block = blocks.data.data[i];
        char typeBuf[129];
        uint32_t typeSize = (uint32_t)block.type.size > 128 ? 128 : (uint32_t)block.type.size;
        MemCpy(typeBuf, block.type.data.data, typeSize);
        typeBuf[typeSize] = 0;
        const char *label = BlockTypeLabel(typeBuf);

        byteview displayName = block.signature.size > 0 ? byteview{block.signature.data.data, block.signature.size}
                                                        : byteview{block.name.data.data, block.name.size};

        if (block.annotation.size > 0)
        {
            pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %.*s %s %.*s\n"_s,
                                  block.startLine + 1, block.endLine + 1, (int)block.annotation.size,
                                  block.annotation.data.data, label, (int)displayName.size, displayName.data);
        }
        else if (displayName.size > 0)
        {
            pos +=
                StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %s %.*s\n"_s,
                               block.startLine + 1, block.endLine + 1, label, (int)displayName.size, displayName.data);
        }
        else
        {
            pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %s\n"_s,
                                  block.startLine + 1, block.endLine + 1, label);
        }
    }

    return AllocCopy(alloc, {buf, pos});
}

// ═══════════════════════════════════════════════════════════════════════════
// xcav tool handlers
// ═══════════════════════════════════════════════════════════════════════════

// ─── xcav_blocks ───────────────────────────────────────────────────────────

static auto HandleXcavBlocks(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);

    byteview safePath = NullTermPath(alloc, file);
    auto blocks = ListBlocks(safePath, alloc);

    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_read ─────────────────────────────────────────────────────────────

static auto HandleXcavRead(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);

    byteview safePath = NullTermPath(alloc, file);

    // Check for --name mode
    byteview nameFilter = ArgString(args, "name");
    uint32_t lineVal = ArgU32(args, "line", 0);
    uint32_t offsetVal = ArgU32(args, "offset", 0);
    uint32_t limitVal = ArgU32(args, "limit", 0);
    bool rawMode = ArgBool(args, "raw", false);
    bool showNumbers = ArgBool(args, "numbers", false);

    // ── Default: print entire file ──
    if (nameFilter.size == 0 && lineVal == 0 && offsetVal == 0)
    {
        file_handle fh = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh))
            FMT_RESULT(true, "ERROR: cannot open file '%.*s'"_s, (int)file.size, file.data);

        byteview text = FileReadFully(alloc, fh);
        FileClose(fh);
        if (text.size == 0)
            FMT_RESULT(true, "ERROR: empty file '%.*s'"_s, (int)file.size, file.data);

        return MakeResult(alloc, false, text.data, text.size);
    }

    // ── name mode ──
    if (nameFilter.size > 0)
    {
        auto blocks = ListBlocks(safePath, alloc);
        for (uint64_t i = 0; i < blocks.size; ++i)
        {
            read_block_info info = ReadBlock(safePath, blocks.data.data[i].startLine, alloc, rawMode);
            if (info.text.size == 0)
                continue;

            bool match = false;
            // Exact match
            if (info.path.size == nameFilter.size)
            {
                match = true;
                for (uint32_t j = 0; j < info.path.size; ++j)
                    if (info.path.data[j] != nameFilter.data[j])
                        match = false;
            }
            // Suffix match (e.g. "GetX" matches "Point::GetX")
            if (!match && info.path.size > nameFilter.size + 2)
            {
                uint32_t suffixStart = info.path.size - nameFilter.size;
                if (info.path.data[suffixStart - 1] == ':' && info.path.data[suffixStart - 2] == ':')
                {
                    match = true;
                    for (uint32_t j = 0; j < nameFilter.size; ++j)
                        if (info.path.data[suffixStart + j] != nameFilter.data[j])
                            match = false;
                }
            }
            if (match)
            {
                byteview output = info.text;
                if (showNumbers)
                {
                    // TODO: add line numbers to output if needed
                }
                return MakeResult(alloc, false, output.data, output.size);
            }
        }
        FMT_RESULT(true, "ERROR: no block matching '%.*s' in '%.*s'"_s, (int)nameFilter.size, nameFilter.data,
                   (int)file.size, file.data);
    }

    // ── offset mode ──
    if (offsetVal > 0)
    {
        // Plain file reading with offset/limit
        file_handle fh = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh))
            FMT_RESULT(true, "ERROR: cannot open file '%.*s'"_s, (int)file.size, file.data);

        byteview text = FileReadFully(alloc, fh);
        FileClose(fh);

        // Find line boundaries
        uint32_t lineStart = 0;
        uint32_t lineIdx = 1;
        while (lineStart < text.size && lineIdx < offsetVal)
        {
            if (text.data[lineStart] == '\n')
                ++lineIdx;
            ++lineStart;
        }
        if (lineIdx < offsetVal)
            FMT_RESULT(true, "ERROR: offset %u exceeds file length"_s, offsetVal);

        uint32_t endPos = lineStart;
        uint32_t linesToRead = limitVal > 0 ? limitVal : 0xFFFFFFFF;
        for (uint32_t l = 0; l < linesToRead && endPos < text.size; ++l)
        {
            while (endPos < text.size && text.data[endPos] != '\n')
                ++endPos;
            if (endPos < text.size)
                ++endPos; // skip newline
        }

        return MakeResult(alloc, false, text.data + lineStart, endPos - lineStart);
    }

    // ── line mode ──
    if (lineVal > 0)
    {
        read_block_info info = ReadBlock(safePath, lineVal - 1, alloc, rawMode);
        if (info.text.size == 0)
            FMT_RESULT(true, "ERROR: no block at line %u in '%.*s'"_s, lineVal, (int)file.size, file.data);

        return MakeResult(alloc, false, info.text.data, info.text.size);
    }

    FMT_RESULT(true, "ERROR: expected one of: line, name, offset, or no args for full file"_s);
}

// ─── xcav_move ─────────────────────────────────────────────────────────────

static auto HandleXcavMove(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    uint32_t lineVal = ArgU32(args, "line", 0);
    uint32_t destVal = ArgU32(args, "destLine", 0);

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);
    if (lineVal < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'line' (must be >= 1)"_s);
    if (destVal < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'destLine' (must be >= 1)"_s);

    byteview safePath = NullTermPath(alloc, file);
    bool ok = MoveBlock(safePath, lineVal - 1, destVal - 1, alloc);
    if (!ok)
        FMT_RESULT(true, "ERROR: move failed"_s);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_move_into ────────────────────────────────────────────────────────

static auto HandleXcavMoveInto(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview srcFile = ArgString(args, "srcFile");
    uint32_t srcLine = ArgU32(args, "srcLine", 0);
    byteview dstFile = ArgString(args, "dstFile");
    uint32_t dstLine = ArgU32(args, "dstLine", 0);
    bool copyIncludes = ArgBool(args, "copyIncludes", false);

    if (srcFile.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'srcFile'"_s);
    if (srcLine < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'srcLine' (must be >= 1)"_s);
    if (dstFile.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'dstFile'"_s);
    if (dstLine < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'dstLine' (must be >= 1)"_s);

    byteview safeSrc = NullTermPath(alloc, srcFile);
    byteview safeDst = NullTermPath(alloc, dstFile);

    bool ok = MoveBlockInto(safeSrc, srcLine - 1, safeDst, dstLine - 1, copyIncludes, alloc);
    if (!ok)
        FMT_RESULT(true, "ERROR: move-into failed"_s);

    // Show blocks for both files
    auto srcBlocks = ListBlocks(safeSrc, alloc);
    auto dstBlocks = ListBlocks(safeDst, alloc);

    uint8_t buf[8192];
    uint32_t pos = 0;
    pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "%.*s (%llu blocks)\n"_s, (int)srcFile.size,
                          srcFile.data, (unsigned long long)srcBlocks.size);
    for (uint64_t i = 0; i < srcBlocks.size && pos + 256 < sizeof(buf); ++i)
    {
        block_info const &b = srcBlocks.data.data[i];
        pos +=
            StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %.*s %.*s\n"_s, b.startLine + 1,
                           b.endLine + 1, (int)b.type.size, b.type.data.data, (int)b.name.size, b.name.data.data);
    }
    pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "\n%.*s (%llu blocks)\n"_s,
                          (int)dstFile.size, dstFile.data, (unsigned long long)dstBlocks.size);
    for (uint64_t i = 0; i < dstBlocks.size && pos + 256 < sizeof(buf); ++i)
    {
        block_info const &b = dstBlocks.data.data[i];
        pos +=
            StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %.*s %.*s\n"_s, b.startLine + 1,
                           b.endLine + 1, (int)b.type.size, b.type.data.data, (int)b.name.size, b.name.data.data);
    }

    return MakeResult(alloc, false, buf, pos);
}

// ─── xcav_delete ───────────────────────────────────────────────────────────

static auto HandleXcavDelete(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    uint32_t lineVal = ArgU32(args, "line", 0);

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);
    if (lineVal < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'line' (must be >= 1)"_s);

    byteview safePath = NullTermPath(alloc, file);
    bool ok = DeleteBlock(safePath, lineVal - 1, alloc);
    if (!ok)
        FMT_RESULT(true, "ERROR: delete failed"_s);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_replace ──────────────────────────────────────────────────────────

static auto HandleXcavReplace(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    uint32_t lineVal = ArgU32(args, "line", 0);
    byteview oldText = ArgString(args, "oldText");
    byteview newText = ArgString(args, "newText");

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);
    if (lineVal < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'line' (must be >= 1)"_s);
    if (oldText.size == 0 && newText.size == 0)
        FMT_RESULT(true, "ERROR: at least one of 'oldText' or 'newText' required"_s);

    byteview safePath = NullTermPath(alloc, file);
    bool ok = ReplaceInBlock(safePath, lineVal - 1, oldText, newText, alloc);
    if (!ok)
        FMT_RESULT(true, "ERROR: replace failed — oldText not found in block"_s);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_replace_block ────────────────────────────────────────────────────

static auto HandleXcavReplaceBlock(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    uint32_t lineVal = ArgU32(args, "line", 0);
    byteview newText = ArgString(args, "newText");

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);
    if (lineVal < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'line' (must be >= 1)"_s);

    byteview safePath = NullTermPath(alloc, file);

    // Write newText to temp file (ReplaceBlock takes a file path)
    byteview tempPath = WriteTempFile(alloc, newText, "replace");
    if (tempPath.size == 0)
        FMT_RESULT(true, "ERROR: failed to create temp file for replacement content"_s);

    bool ok = ReplaceBlock(safePath, lineVal - 1, tempPath, alloc);
    if (!ok)
        FMT_RESULT(true, "ERROR: replace-block failed"_s);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_undo ─────────────────────────────────────────────────────────────

static auto HandleXcavUndo(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);

    byteview safePath = NullTermPath(alloc, file);
    bool ok = RestoreBackup(safePath, alloc);
    if (!ok)
        FMT_RESULT(true, "ERROR: undo failed — no backup available for '%.*s'"_s, (int)file.size, file.data);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_edit ─────────────────────────────────────────────────────────────

static auto HandleXcavEdit(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    byteview oldText = ArgString(args, "oldText");
    byteview newText = ArgString(args, "newText");

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);
    if (oldText.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'oldText'"_s);

    byteview safePath = NullTermPath(alloc, file);

    // Write oldText and newText to temp files (EditSafe takes file paths)
    byteview oldPath = WriteTempFile(alloc, oldText, "old");
    byteview newPath = WriteTempFile(alloc, newText, "new");
    if (oldPath.size == 0 || (newText.size > 0 && newPath.size == 0))
        FMT_RESULT(true, "ERROR: failed to create temp files for edit"_s);

    bool ok = EditSafe(safePath, oldPath, newPath.size > 0 ? newPath : oldPath, alloc, false);
    if (!ok)
        FMT_RESULT(true, "ERROR: edit failed — oldText not found in file"_s);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ─── xcav_copy ─────────────────────────────────────────────────────────────

static auto HandleXcavCopy(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview srcFile = ArgString(args, "srcFile");
    uint32_t srcLine = ArgU32(args, "srcLine", 0);
    byteview dstFile = ArgString(args, "dstFile");
    uint32_t dstLine = ArgU32(args, "dstLine", 0);
    bool copyIncludes = ArgBool(args, "copyIncludes", false);

    if (srcFile.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'srcFile'"_s);
    if (srcLine < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'srcLine' (must be >= 1)"_s);
    if (dstFile.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'dstFile'"_s);

    byteview safeSrc = NullTermPath(alloc, srcFile);
    byteview safeDst = NullTermPath(alloc, dstFile);

    CopyResult result = CopyBlock(safeSrc, srcLine - 1, safeDst, dstLine, false, copyIncludes, alloc);
    if (!result.ok)
        FMT_RESULT(true, "ERROR: copy failed — %s"_s, result.error ? result.error : "unknown error");

    // Show blocks for both files
    auto srcBlocks = ListBlocks(safeSrc, alloc);
    auto dstBlocks = ListBlocks(safeDst, alloc);

    uint8_t buf[8192];
    uint32_t pos = 0;
    pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "%.*s (%llu blocks)\n"_s, (int)srcFile.size,
                          srcFile.data, (unsigned long long)srcBlocks.size);
    for (uint64_t i = 0; i < srcBlocks.size && pos + 256 < sizeof(buf); ++i)
    {
        block_info const &b = srcBlocks.data.data[i];
        pos +=
            StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %.*s %.*s\n"_s, b.startLine + 1,
                           b.endLine + 1, (int)b.type.size, b.type.data.data, (int)b.name.size, b.name.data.data);
    }
    pos += StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "\n%.*s (%llu blocks)\n"_s,
                          (int)dstFile.size, dstFile.data, (unsigned long long)dstBlocks.size);
    for (uint64_t i = 0; i < dstBlocks.size && pos + 256 < sizeof(buf); ++i)
    {
        block_info const &b = dstBlocks.data.data[i];
        pos +=
            StringWriteFmt(span<uint8_t>{buf + pos, sizeof(buf) - pos - 1}, "  %4u-%4u %.*s %.*s\n"_s, b.startLine + 1,
                           b.endLine + 1, (int)b.type.size, b.type.data.data, (int)b.name.size, b.name.data.data);
    }

    return MakeResult(alloc, false, buf, pos);
}

// ─── xcav_insert ───────────────────────────────────────────────────────────

static auto HandleXcavInsert(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview file = ArgString(args, "file");
    uint32_t lineVal = ArgU32(args, "line", 0);
    byteview content = ArgString(args, "content");
    byteview mode = ArgString(args, "mode");

    if (file.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'file'"_s);
    if (lineVal < 1)
        FMT_RESULT(true, "ERROR: missing or invalid 'line' (must be >= 1)"_s);
    if (content.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'content'"_s);
    if (mode.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'mode' (must be 'before' or 'after')"_s);

    byteview safePath = NullTermPath(alloc, file);
    byteview tempPath = WriteTempFile(alloc, content, "insert");
    if (tempPath.size == 0)
        FMT_RESULT(true, "ERROR: failed to create temp file for insert content"_s);

    bool ok;
    if (ByteviewEq(mode, "before") || ByteviewEq(mode, "--before"))
        ok = InsertBeforeBlock(safePath, lineVal - 1, tempPath, alloc);
    else if (ByteviewEq(mode, "after") || ByteviewEq(mode, "--after"))
        ok = InsertAfterBlock(safePath, lineVal - 1, tempPath, alloc);
    else
        FMT_RESULT(true, "ERROR: mode must be 'before' or 'after', got '%.*s'"_s, (int)mode.size, mode.data);

    if (!ok)
        FMT_RESULT(true, "ERROR: insert failed"_s);

    auto blocks = ListBlocks(safePath, alloc);
    byteview output = FmtBlockList(alloc, file, blocks);
    return MakeResult(alloc, false, output.data, output.size);
}

// ═══════════════════════════════════════════════════════════════════════════
// System tool handlers
// ═══════════════════════════════════════════════════════════════════════════

// ─── bash ──────────────────────────────────────────────────────────────────

static auto HandleBash(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview command = ArgString(args, "command");

    if (command.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'command'"_s);

    // Build argument array: bash, -c, command, nullptr
    const char *cmdArr[4];
    cmdArr[0] = "bash";
    cmdArr[1] = "-c";

    // Null-terminate the command string
    span<uint8_t> cmdBuf = RegionAlloc::AllocArray<uint8_t>(alloc, command.size + 1);
    MemCpy(cmdBuf.data, command.data, command.size);
    cmdBuf.data[command.size] = 0;
    cmdArr[2] = (const char *)cmdBuf.data;
    cmdArr[3] = nullptr;

    subprocess_result result = SubprocessRun(span<const char *const>{cmdArr, 4}, alloc, {}, 30000);
    // Combine stdout and stderr into result
    uint32_t totalSize = result.stdout_data.size + result.stderr_data.size + 256;
    span<uint8_t> outBuf = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    uint32_t pos = 0;

    if (result.stdout_data.size > 0)
    {
        MemCpy(outBuf.data + pos, result.stdout_data.data, result.stdout_data.size);
        pos += result.stdout_data.size;
    }
    if (result.stderr_data.size > 0)
    {
        if (pos > 0)
            outBuf.data[pos++] = '\n';
        MemCpy(outBuf.data + pos, result.stderr_data.data, result.stderr_data.size);
        pos += result.stderr_data.size;
    }

    if (result.timed_out)
        pos += StringWriteFmt(span<uint8_t>{outBuf.data + pos, totalSize - pos}, "\n[TIMED OUT after 30s]"_s);
    else if (result.exit_code != 0)
        pos +=
            StringWriteFmt(span<uint8_t>{outBuf.data + pos, totalSize - pos}, "\n[exit code: %d]"_s, result.exit_code);

    ToolResult tr;
    tr.isError = (result.timed_out || result.exit_code != 0);
    tr.content = {outBuf.data, pos};
    return tr;
}

// ─── read_file ─────────────────────────────────────────────────────────────

static auto HandleReadFile(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview path = ArgString(args, "path");

    if (path.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'path'"_s);

    byteview safePath = NullTermPath(alloc, path);
    file_handle fh = FileOpen(safePath, FileOpenMode::Read);
    if (!FileValid(fh))
        FMT_RESULT(true, "ERROR: cannot open file '%.*s'"_s, (int)path.size, path.data);

    byteview content = FileReadFully(alloc, fh);
    FileClose(fh);

    ToolResult r;
    r.isError = false;
    r.content = content;
    return r;
}

// ─── write_file ────────────────────────────────────────────────────────────

static auto HandleWriteFile(byteview argsJson, region_alloc &alloc) -> ToolResult
{
    json_value *args = ParseArgs(argsJson, alloc);
    byteview path = ArgString(args, "path");
    byteview content = ArgString(args, "content");

    if (path.size == 0)
        FMT_RESULT(true, "ERROR: missing required argument 'path'"_s);

    byteview safePath = NullTermPath(alloc, path);

    // Create parent directories if needed
    for (uint32_t i = 0; i < path.size; ++i)
    {
        if (path.data[i] == '/' && i > 0)
        {
            // Check if this directory component exists
            byteview prefix = {path.data, i};
            byteview safePrefix = NullTermPath(alloc, prefix);
            file_handle testFh = FileOpen(safePrefix, FileOpenMode::Read);
            if (!FileValid(testFh))
            {
                CreateDirectory(safePrefix);
            }
            else
            {
                FileClose(testFh);
            }
        }
    }

    file_handle fh = FileOpen(safePath, FileOpenMode::Write);
    if (!FileValid(fh))
        FMT_RESULT(true, "ERROR: cannot write file '%.*s'"_s, (int)path.size, path.data);

    uint32_t written = FileWrite(fh, content.size, content.data);
    FileClose(fh);

    if (written != content.size)
        FMT_RESULT(true, "ERROR: short write to '%.*s' (%u of %u bytes)"_s, (int)path.size, path.data, written,
                   content.size);

    FMT_RESULT(false, "Wrote %u bytes to '%.*s'"_s, written, (int)path.size, path.data);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool registry
// ═══════════════════════════════════════════════════════════════════════════

// JSON Schema snippets for tool parameters
namespace schemas
{

const char *kBlocks =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"}},"required":["file"]})JSCH";

const char *kRead =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"line":{"type":"integer","description":"1-indexed line number of block to read"},"name":{"type":"string","description":"Structural name to find (e.g. 'GetX' matches 'Point::GetX')"},"offset":{"type":"integer","description":"1-indexed line offset to start reading"},"limit":{"type":"integer","description":"Max lines to read from offset"},"raw":{"type":"boolean","description":"Keep original indentation"},"numbers":{"type":"boolean","description":"Include line numbers"}},"required":["file"]})JSCH";

const char *kMove =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"line":{"type":"integer","description":"1-indexed line of block to move"},"destLine":{"type":"integer","description":"1-indexed destination line"}},"required":["file","line","destLine"]})JSCH";

const char *kMoveInto =
    R"JSCH({"type":"object","properties":{"srcFile":{"type":"string","description":"Source file path"},"srcLine":{"type":"integer","description":"1-indexed line of block to move"},"dstFile":{"type":"string","description":"Destination file path"},"dstLine":{"type":"integer","description":"1-indexed destination line"},"copyIncludes":{"type":"boolean","description":"Copy #include lines from source to dest"}},"required":["srcFile","srcLine","dstFile","dstLine"]})JSCH";

const char *kDelete =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"line":{"type":"integer","description":"1-indexed line of block to delete"}},"required":["file","line"]})JSCH";

const char *kReplace =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"line":{"type":"integer","description":"1-indexed line within the block"},"oldText":{"type":"string","description":"Text to replace (only needs uniqueness within block)"},"newText":{"type":"string","description":"Replacement text"}},"required":["file","line"]})JSCH";

const char *kReplaceBlock =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"line":{"type":"integer","description":"1-indexed line of block to replace"},"newText":{"type":"string","description":"Complete replacement content for the block"}},"required":["file","line","newText"]})JSCH";

const char *kUndo =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"}},"required":["file"]})JSCH";

const char *kEdit =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"oldText":{"type":"string","description":"Text to find (line-based matching, whitespace-insensitive)"},"newText":{"type":"string","description":"Replacement text"}},"required":["file","oldText","newText"]})JSCH";

const char *kCopy =
    R"JSCH({"type":"object","properties":{"srcFile":{"type":"string","description":"Source file path"},"srcLine":{"type":"integer","description":"1-indexed line of block to copy"},"dstFile":{"type":"string","description":"Destination file path"},"dstLine":{"type":"integer","description":"1-indexed destination line (0 for file start)"},"copyIncludes":{"type":"boolean","description":"Copy #include lines from source to dest"}},"required":["srcFile","srcLine","dstFile","dstLine"]})JSCH";

const char *kInsert =
    R"JSCH({"type":"object","properties":{"file":{"type":"string","description":"Path to source file"},"line":{"type":"integer","description":"1-indexed line of reference block"},"content":{"type":"string","description":"Code to insert"},"mode":{"type":"string","description":"'before' or 'after' the reference block"}},"required":["file","line","content","mode"]})JSCH";

const char *kBash =
    R"JSCH({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute"}},"required":["command"]})JSCH";

const char *kReadFile =
    R"JSCH({"type":"object","properties":{"path":{"type":"string","description":"Path to file to read"}},"required":["path"]})JSCH";

const char *kWriteFile =
    R"JSCH({"type":"object","properties":{"path":{"type":"string","description":"Path to file to write"},"content":{"type":"string","description":"Content to write"}},"required":["path","content"]})JSCH";

} // namespace schemas

// ─── Tool definitions ──────────────────────────────────────────────────────

static ToolDef s_toolDefs[] = {
    // ── xcav tools ─────────────────────────────────────────────────────────
    {"xcav_blocks",
     "List structural blocks (functions, classes, etc.) in a C/C++/Java/TS/JS source file. "
     "Returns 1-indexed line ranges with block types and names.",
     schemas::kBlocks, HandleXcavBlocks},

    {"xcav_read",
     "Read code from a file. Without arguments, prints the entire file un-indented. "
     "Use 'line' to read a specific structural block (output is directly usable as oldText for xcav_edit). "
     "Use 'name' to find a block by structural name (supports suffix matching). "
     "Use 'offset'/'limit' for plain line range reading.",
     schemas::kRead, HandleXcavRead},

    {"xcav_move",
     "Move a structural code block to a new location within the same file. "
     "Re-indents to match destination. Destination must be a block boundary.",
     schemas::kMove, HandleXcavMove},

    {"xcav_move_into",
     "Move a structural code block from one file to another. "
     "Auto-strips 'static' from moved functions. Use copyIncludes to copy #include lines.",
     schemas::kMoveInto, HandleXcavMoveInto},

    {"xcav_delete", "Delete a structural code block. Cleans up surrounding whitespace and orphaned comments.",
     schemas::kDelete, HandleXcavDelete},

    {"xcav_replace",
     "Replace text within a structural block. oldText only needs uniqueness within the block "
     "(unlike xcav_edit which searches the entire file). For scoped replacements inside a specific "
     "function/class.",
     schemas::kReplace, HandleXcavReplace},

    {"xcav_replace_block",
     "Replace an entire structural block with new content. "
     "The new content replaces the block at the given line. Atomic — safe for last block in a namespace.",
     schemas::kReplaceBlock, HandleXcavReplaceBlock},

    {"xcav_undo",
     "Undo the last xcav operation (move, delete, edit, etc.) on a file. "
     "Restores from the most recent backup. Supports up to 20 levels of undo.",
     schemas::kUndo, HandleXcavUndo},

    {"xcav_edit",
     "Safe line-based text replacement in a file. Matches oldText by line content "
     "(whitespace-insensitive per line). Copies indentation from matched lines to replacement. "
     "Use for all file edits. oldText from xcav_read output is directly usable.",
     schemas::kEdit, HandleXcavEdit},

    {"xcav_copy",
     "Copy a structural code block from one file to another. Source is unaffected. "
     "Combine with xcav_delete for extract-to-new-file pattern.",
     schemas::kCopy, HandleXcavCopy},

    {"xcav_insert",
     "Insert code before or after a structural block. Content is re-indented to match. "
     "Mode must be 'before' or 'after'.",
     schemas::kInsert, HandleXcavInsert},

    // ── System tools ───────────────────────────────────────────────────────
    {"bash",
     "Execute a bash command. Returns stdout and stderr combined. 30-second timeout. "
     "Use for file operations (ls, grep, find), build commands, and running tests.",
     schemas::kBash, HandleBash},

    {"read_file",
     "Read the full contents of a file. For structural code reading, prefer xcav_read. "
     "Use this for non-code files (markdown, config, etc.) or when you need exact raw content.",
     schemas::kReadFile, HandleReadFile},

    {"write_file",
     "Write content to a file. Creates parent directories as needed. "
     "Overwrites existing files. Use xcav_edit for targeted code changes instead.",
     schemas::kWriteFile, HandleWriteFile},
};

static constexpr uint32_t kToolCount = sizeof(s_toolDefs) / sizeof(s_toolDefs[0]);

// ─── Init / Dispatch / Query ───────────────────────────────────────────────

void InitToolRegistry()
{
    // Static initialization — tool defs are already populated.
    // This function exists for future dynamic registration (e.g. project-specific tools).
}

auto ToolRegistryDispatch(byteview /*toolCallId*/, byteview toolName, byteview toolArguments, region_alloc &alloc)
    -> ToolResult
{
    for (uint32_t i = 0; i < kToolCount; ++i)
    {
        if (ByteviewEq(toolName, s_toolDefs[i].name))
            return s_toolDefs[i].handler(toolArguments, alloc);
    }

    FMT_RESULT(true, "Unknown tool: %.*s"_s, (int)toolName.size, toolName.data);
}

auto GetToolDefs() -> span<const ToolDef>
{
    return {s_toolDefs, kToolCount};
}

} // namespace nyla
