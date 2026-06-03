// ─── Xcav usage tracking — local JSONL, no telemetry ───────────────────────
// Appends one JSON event per invocation to ~/.xcav/usage/events.jsonl.
// No STL, no <chrono>, no exceptions. Uses nyla APIs + raw POSIX.

#include "xcav/usage_log.h"

#include "xcav/language.h"
#include "xcav/text_util.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

#include <stdio.h>    // snprintf
#include <stdlib.h>   // getenv
#include <sys/stat.h> // mkdir
#include <time.h>     // clock_gettime, gmtime_r
#include <unistd.h>   // getpid

namespace nyla
{

// ─── Helpers ───────────────────────────────────────────────────────────────

// Make a byteview from a C string literal.
static auto CStrView(const char *s) -> byteview
{
    return byteview{(const uint8_t *)s, (uint64_t)strlen(s)};
}

// ─── Path construction ─────────────────────────────────────────────────────

// Returns a null-terminated byteview for a sub-path under $HOME.
// E.g. HomePath(alloc, ".xcav/usage") -> "$HOME/.xcav/usage\0"
static auto HomePath(region_alloc *alloc, const char *subpath) -> byteview
{
    const char *home = getenv("HOME");
    uint64_t homeLen = home ? strlen(home) : 0;
    uint64_t subLen = strlen(subpath);
    uint64_t totalLen = homeLen + 1 + subLen;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(*alloc, totalLen + 1);
    if (homeLen > 0)
    {
        MemCpy(buf.data, home, homeLen);
        buf.data[homeLen] = '/';
        MemCpy(buf.data + homeLen + 1, subpath, subLen);
    }
    else
    {
        // Fallback: relative path
        MemCpy(buf.data, subpath, subLen);
    }
    buf.data[totalLen] = 0;
    return byteview{buf.data, totalLen};
}

// ─── Boot ID (cached, read once from /proc) ────────────────────────────────

static bool s_bootIdCached = false;
static char s_bootId[64]; // UUID is 36 chars + newline

static auto GetBootId() -> const char *
{
    if (s_bootIdCached)
        return s_bootId;
    s_bootIdCached = true;

    file_handle fh = FileOpen("/proc/sys/kernel/random/boot_id"_s, FileOpenMode::Read);
    if (!FileValid(fh))
    {
        s_bootId[0] = 0;
        return s_bootId;
    }
    uint32_t n = FileRead(fh, sizeof(s_bootId) - 1, (uint8_t *)s_bootId);
    FileClose(fh);
    if (n > 0 && s_bootId[n - 1] == '\n')
        --n;
    s_bootId[n] = 0;
    return s_bootId;
}

// ─── JSONL file helpers ────────────────────────────────────────────────────

// Read the last non-empty line of the JSONL file (for seq + prev_cmd).
// Returns empty byteview if file doesn't exist or is empty/corrupt.
static auto ReadLastJsonlLine(region_alloc *alloc, byteview jsonlPath) -> byteview
{
    file_handle fh = FileOpen(jsonlPath, FileOpenMode::Read);
    if (!FileValid(fh))
        return {};

    byteview content = FileReadFully(*alloc, fh);
    FileClose(fh);

    if (content.size == 0)
        return {};

    // Find last non-empty line (skip trailing newlines)
    uint64_t end = content.size;
    while (end > 0 && (content.data[end - 1] == '\n' || content.data[end - 1] == '\r'))
        --end;
    if (end == 0)
        return {};

    // Walk backwards to find the previous newline
    uint64_t start = end;
    while (start > 0 && content.data[start - 1] != '\n')
        --start;

    return byteview{content.data + start, end - start};
}

// Parse an integer JSON value like "seq": 42 from within a byteview.
// Returns 0 if not found.
static auto ParseJsonInt(byteview json, const char *key) -> int
{
    uint64_t keyLen = strlen(key);
    for (uint64_t i = 0; i + keyLen + 4 <= json.size; ++i)
    {
        if (json.data[i] != '"')
            continue;
        if (i + 1 + keyLen >= json.size)
            break;
        if (!MemEq(json.data + i + 1, key, keyLen))
            continue;
        uint64_t afterKey = i + 1 + keyLen;
        if (json.data[afterKey] != '"')
            continue;
        uint64_t p = afterKey + 1;
        while (p < json.size && (json.data[p] == ' ' || json.data[p] == ':'))
            ++p;
        if (p >= json.size || (json.data[p] < '0' || json.data[p] > '9'))
            continue;

        int val = 0;
        while (p < json.size && json.data[p] >= '0' && json.data[p] <= '9')
        {
            val = val * 10 + (json.data[p] - '0');
            ++p;
        }
        return val;
    }
    return 0;
}

// Parse a string JSON value like "cmd": "edit" from within a byteview.
// Returns empty byteview if not found.
static auto ParseJsonString(byteview json, const char *key) -> byteview
{
    uint64_t keyLen = strlen(key);
    for (uint64_t i = 0; i + keyLen + 5 <= json.size; ++i)
    {
        if (json.data[i] != '"')
            continue;
        if (!MemEq(json.data + i + 1, key, keyLen))
            continue;
        uint64_t afterKey = i + 1 + keyLen;
        if (json.data[afterKey] != '"')
            continue;
        uint64_t p = afterKey + 1;
        while (p < json.size && (json.data[p] == ' ' || json.data[p] == ':'))
            ++p;
        if (p >= json.size || json.data[p] != '"')
            continue;
        ++p; // skip opening quote
        uint64_t valStart = p;
        while (p < json.size && json.data[p] != '"')
            ++p;
        return byteview{json.data + valStart, p - valStart};
    }
    return {};
}

// ─── Category mapping ──────────────────────────────────────────────────────

static auto CmdToCategory(byteview cmd) -> const char *
{
    if (ByteviewEq(cmd, "blocks") || ByteviewEq(cmd, "read"))
        return "survey";
    if (ByteviewEq(cmd, "move") || ByteviewEq(cmd, "move-into") || ByteviewEq(cmd, "delete") ||
        ByteviewEq(cmd, "edit") || ByteviewEq(cmd, "replace") || ByteviewEq(cmd, "replace-block") ||
        ByteviewEq(cmd, "copy"))
        return "mutation";
    if (ByteviewEq(cmd, "undo"))
        return "recovery";
    return "help";
}

// ─── Language label ────────────────────────────────────────────────────────

static auto LangLabel(source_language lang) -> const char *
{
    switch (lang)
    {
    case source_language::C:
        return "c";
    case source_language::Cpp:
        return "cpp";
    case source_language::Java:
        return "java";
    case source_language::JavaScript:
        return "javascript";
    case source_language::TypeScript:
        return "typescript";
    case source_language::Tsx:
        return "tsx";
    default:
        return "unknown";
    }
}

// ─── File extension from path ──────────────────────────────────────────────

static auto PathExt(byteview path) -> byteview
{
    for (uint64_t i = path.size; i > 0; --i)
    {
        if (path.data[i - 1] == '.')
            return byteview{path.data + i - 1, path.size - (i - 1)};
        if (path.data[i - 1] == '/')
            break;
    }
    return {};
}

// ─── JSON string escaping ──────────────────────────────────────────────────

// Returns required length after escaping " and \ and control chars.
// Writes escaped string into out (must be pre-sized). Returns bytes written.
static auto EscapeJson(char *out, byteview in) -> uint32_t
{
    uint32_t w = 0;
    for (uint64_t i = 0; i < in.size; ++i)
    {
        uint8_t c = in.data[i];
        if (c == '"' || c == '\\')
        {
            out[w++] = '\\';
            out[w++] = (char)c;
        }
        else if (c == '\n')
        {
            out[w++] = '\\';
            out[w++] = 'n';
        }
        else if (c == '\r')
        {
            out[w++] = '\\';
            out[w++] = 'r';
        }
        else if (c == '\t')
        {
            out[w++] = '\\';
            out[w++] = 't';
        }
        else if (c < 0x20)
        {
            w += (uint32_t)snprintf(out + w, 8, "\\u%04x", c);
        }
        else
        {
            out[w++] = (char)c;
        }
    }
    return w;
}

// ─── Timestamp formatting ──────────────────────────────────────────────────

// ISO 8601: "2026-06-02T12:34:56.123Z"
static auto FormatTimestamp(char *out, uint32_t outSize) -> uint32_t
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);

    int millis = (int)(ts.tv_nsec / 1000000);

    return (uint32_t)snprintf(out, outSize, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                              tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, millis);
}

// ─── Public API ────────────────────────────────────────────────────────────

void LogUsage(region_alloc *alloc, const cli_args &args, struct timespec t_start, int exit_code, const char *error_tag)
{
    // ── Ensure ~/.xcav/usage/ exists ──
    byteview xcavDir = HomePath(alloc, ".xcav");
    mkdir(Span::CStr(xcavDir), 0755);
    byteview usageDir = HomePath(alloc, ".xcav/usage");
    mkdir(Span::CStr(usageDir), 0755);
    byteview jsonlPath = HomePath(alloc, ".xcav/usage/events.jsonl");

    // ── Compute duration ──
    struct timespec t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    int64_t ms = (t_end.tv_sec - t_start.tv_sec) * 1000 + (t_end.tv_nsec - t_start.tv_nsec) / 1000000;
    if (ms < 0)
        ms = 0;

    // ── Read previous line for seq + prev_cmd ──
    byteview lastLine = ReadLastJsonlLine(alloc, jsonlPath);
    int seq = 1;
    byteview prevCmd{};
    if (lastLine.size > 0)
    {
        seq = ParseJsonInt(lastLine, "seq") + 1;
        prevCmd = ParseJsonString(lastLine, "cmd");
    }

    // ── Command ──
    byteview cmd = args.command;

    // ── Category ──
    const char *cat = CmdToCategory(cmd);

    // ── Session ID ──
    uint32_t pid = (uint32_t)getpid();
    const char *bootId = GetBootId();

    // ── Timestamp ──
    char tsBuf[32];
    uint32_t tsLen = FormatTimestamp(tsBuf, sizeof(tsBuf));

    // ── Outcome ──
    bool ok = (exit_code == 0);
    bool mutated = ByteviewEq(cmd, "move") || ByteviewEq(cmd, "move-into") || ByteviewEq(cmd, "delete") ||
                   ByteviewEq(cmd, "edit") || ByteviewEq(cmd, "replace") || ByteviewEq(cmd, "replace-block") ||
                   ByteviewEq(cmd, "copy") || ByteviewEq(cmd, "undo");

    // ── Target ──
    byteview targetPath{};
    byteview targetExt{};
    const char *targetLang = "unknown";

    bool isHelpCmd = ByteviewEq(cmd, "help") || ByteviewEq(cmd, "onboard");
    if (!isHelpCmd && args.positional.size >= 2)
    {
        targetPath = args.positional.data.data[1];
        // Truncate to 512 chars
        if (targetPath.size > 512)
            targetPath.size = 512;
        targetExt = PathExt(targetPath);
        source_language lang = DetectLanguage(targetPath);
        targetLang = LangLabel(lang);
    }

    // ── Args flags ──
    bool hasDst = ByteviewEq(cmd, "move") || ByteviewEq(cmd, "move-into") || ByteviewEq(cmd, "copy");
    bool crossFile = ByteviewEq(cmd, "move-into") || ByteviewEq(cmd, "copy");
    bool copyIncludes = false;
    bool hasLine = false;
    const char *mode = nullptr;

    // Check for --copy-includes flag
    for (uint64_t i = 0; i < args.positional.size; ++i)
    {
        if (ByteviewEq(args.positional.data.data[i], "--copy-includes"))
            copyIncludes = true;
    }

    if (ByteviewEq(cmd, "read"))
    {
        bool hasAll = false;
        bool hasName = false;
        bool hasOffset = false;
        bool foundLineNum = false;

        for (uint64_t i = 2; i < args.positional.size; ++i)
        {
            byteview arg = args.positional.data.data[i];
            if (ByteviewEq(arg, "--all"))
                hasAll = true;
            else if (ByteviewEq(arg, "--name"))
                hasName = true;
            else if (ByteviewEq(arg, "--offset"))
                hasOffset = true;
            else if (!foundLineNum && arg.size > 0 && arg.data[0] >= '0' && arg.data[0] <= '9')
                foundLineNum = true;
        }

        hasLine = foundLineNum;
        if (hasName)
            mode = "name";
        else if (hasOffset)
            mode = "offset";
        else if (hasLine)
            mode = "block";
        else
            mode = "all";
    }
    else if (ByteviewEq(cmd, "edit"))
    {
        bool stdinMode = false;
        for (uint64_t i = 0; i < args.positional.size; ++i)
        {
            if (ByteviewEq(args.positional.data.data[i], "--stdin"))
                stdinMode = true;
        }
        mode = stdinMode ? "stdin" : "file";
    }

    // ── Redundant blocks detection ──
    bool redundantBlocks = false;
    if (ByteviewEq(cmd, "blocks") && prevCmd.size > 0)
    {
        const char *prevCat = CmdToCategory(prevCmd);
        if (ByteviewEq(CStrView(prevCat), "mutation") || ByteviewEq(CStrView(prevCat), "recovery"))
            redundantBlocks = true;
    }

    // ── Build JSON ──
    uint32_t jsonBufSize = 4096;
    span<uint8_t> jsonBuf = RegionAlloc::AllocArray<uint8_t>(*alloc, jsonBufSize);

    // Escape target path and command for JSON
    char escapedPath[1028];
    uint32_t escapedPathLen = 0;
    if (targetPath.size > 0)
        escapedPathLen = EscapeJson(escapedPath, targetPath);

    char escapedCmd[64];
    uint32_t escapedCmdLen = EscapeJson(escapedCmd, cmd);

    // Build JSON string using snprintf
    int n = snprintf((char *)jsonBuf.data, jsonBufSize,
                     "{"
                     "\"schema_version\":1,"
                     "\"ts\":\"%.*s\","
                     "\"session_id\":\"%u-%.36s\","
                     "\"seq\":%d,"
                     "\"cmd\":\"%.*s\","
                     "\"cat\":\"%s\"",
                     (int)tsLen, tsBuf, pid, bootId, seq, (int)escapedCmdLen, escapedCmd, cat);

    int written = n;

    // target
    if (targetPath.size > 0)
    {
        written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written,
                            ",\"target\":{\"path\":\"%.*s\",\"ext\":\"%.*s\",\"lang\":\"%s\"}", (int)escapedPathLen,
                            escapedPath, (int)targetExt.size, targetExt.data, targetLang);
    }

    // args
    bool hasAnyArg = hasDst || crossFile || copyIncludes || hasLine || (mode != nullptr);
    if (hasAnyArg)
    {
        written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, ",\"args\":{");
        bool first = true;

        if (hasDst)
        {
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "\"has_dst\":true");
            first = false;
        }
        if (crossFile)
        {
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "%s\"cross_file\":true",
                                first ? "" : ",");
            first = false;
        }
        if (copyIncludes)
        {
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "%s\"copy_includes\":true",
                                first ? "" : ",");
            first = false;
        }
        if (hasLine)
        {
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "%s\"has_line\":true",
                                first ? "" : ",");
            first = false;
        }
        if (mode != nullptr)
        {
            char escapedMode[32];
            uint32_t escapedModeLen = EscapeJson(escapedMode, byteview{(const uint8_t *)mode, (uint64_t)strlen(mode)});
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "%s\"mode\":\"%.*s\"",
                                first ? "" : ",", (int)escapedModeLen, escapedMode);
        }

        written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "}");
    }

    // outcome
    written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written,
                        ",\"outcome\":{"
                        "\"ok\":%s,"
                        "\"code\":%d,"
                        "\"ms\":%lld,"
                        "\"mutated\":%s",
                        ok ? "true" : "false", exit_code, (long long)ms, mutated ? "true" : "false");

    if (!ok && error_tag)
    {
        char escapedErr[64];
        uint32_t escapedErrLen =
            EscapeJson(escapedErr, byteview{(const uint8_t *)error_tag, (uint64_t)strlen(error_tag)});
        written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, ",\"error\":\"%.*s\"",
                            (int)escapedErrLen, escapedErr);
    }

    written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "}");

    // ctx
    bool hasCtx = (prevCmd.size > 0) || redundantBlocks;
    if (hasCtx)
    {
        written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, ",\"ctx\":{");
        bool firstCtx = true;

        if (prevCmd.size > 0)
        {
            char escapedPrev[32];
            uint32_t escapedPrevLen = EscapeJson(escapedPrev, prevCmd);
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "\"prev_cmd\":\"%.*s\"",
                                (int)escapedPrevLen, escapedPrev);
            firstCtx = false;
        }

        if (redundantBlocks)
        {
            written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "%s\"redundant_blocks\":true",
                                firstCtx ? "" : ",");
        }

        written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "}");
    }

    // Close JSON
    written += snprintf((char *)jsonBuf.data + written, jsonBufSize - written, "}\n");

    // ── Append to JSONL ──
    if (written < 0 || (uint32_t)written >= jsonBufSize)
        return; // JSON too large, skip (shouldn't happen)

    file_handle fh = FileOpen(jsonlPath, FileOpenMode::Append);
    if (!FileValid(fh))
        return;

    FileWrite(fh, (uint32_t)written, jsonBuf.data);
    FileClose(fh);
}

} // namespace nyla
