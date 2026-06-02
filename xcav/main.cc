// ─── xcav — code-moving and light structural refactoring tool ───
// Headless CLI app built on nyla_commons_headless (no X11 required at runtime).
//
// Usage:
//   xcav blocks <file>             — list structural blocks in a file
//   xcav move <file> <line> <dest> — move the block at <line> to after <dest>
//   xcav edit <file> <old-file> <new-file>  — safe replace with tree-sitter validation
//
// Uses tree-sitter for structural parsing (tolerates syntax errors).

#include "nyla/commons/byteparser.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"
#include "nyla/commons/stringparser.h"

#include <sys/stat.h>

#include "xcav/backup.h"
#include "xcav/editor.h"
#include "xcav/inline.h"

#include <unistd.h> // _exit

namespace nyla
{

// Exit code set by commands on failure.
static int s_exitCode = 0;

namespace
{

struct cli_args
{
    inline_vec<byteview, 16> positional;
    byteview command;
};

// ─── Argument parsing ───────────────────────────────────────────────────────

auto ParseArgs(region_alloc &alloc) -> cli_args
{
    cli_args args{};

    byteview rawArgs[32];
    ParseStdArgs(rawArgs, 32);

    for (uint32_t i = 1; i < 32 && rawArgs[i].size > 0; ++i)
    {
        byteview arg = RegionAlloc::CopyByteView(alloc, rawArgs[i]);
        InlineVec::Append(args.positional, arg);
    }

    if (args.positional.size > 0)
        args.command = args.positional.data.data[0];

    return args;
}

// ─── String matching ────────────────────────────────────────────────────────

static auto ByteviewEq(byteview a, const char *b) -> bool
{
    while (*b)
    {
        if (a.size == 0 || a.data[0] != (uint8_t)*b)
            return false;
        a.data++;
        a.size--;
        b++;
    }
    return a.size == 0;
}

static auto MakeCStringPath(byteview path, region_alloc &alloc) -> byteview
{
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, path.size + 1);
    MemCpy(buf.data, path.data, path.size);
    buf.data[path.size] = 0;
    return byteview{buf.data, path.size};
}

static void PrintBlockLine(block_info const &block)
{
    char typeBuf[129];
    uint32_t typeSize = (uint32_t)block.type.size > 128 ? 128 : (uint32_t)block.type.size;
    MemCpy(typeBuf, block.type.data.data, typeSize);
    typeBuf[typeSize] = 0;

    char const *label = BlockTypeLabel(typeBuf);
    if (block.name.size > 0)
        FileWriteFmt(GetStdout(), "  %4u-%4u %s %.*s\n"_s, block.startLine + 1, block.endLine + 1, label,
                     (int)block.name.size, block.name.data.data);
    else
        FileWriteFmt(GetStdout(), "  %4u-%4u %s\n"_s, block.startLine + 1, block.endLine + 1, label);
}

static void PrintBlockList(byteview displayPath, inline_vec<block_info, 256> const &blocks)
{
    FileWriteFmt(GetStdout(), "%.*s (%llu blocks)\n"_s, (int)displayPath.size, displayPath.data,
                 (unsigned long long)blocks.size);

    for (uint64_t i = 0; i < blocks.size; ++i)
        PrintBlockLine(blocks.data.data[i]);
}

// ─── Command handlers ───────────────────────────────────────────────────────

void CmdBlocks(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 2)
    {
        LOG("Usage: xcav blocks <file-or-dir> [--recurse]");
        return;
    }

    byteview filePath = args.positional.data.data[1];
    bool recurse = false;
    for (uint64_t i = 2; i < args.positional.size; ++i)
    {
        if (ByteviewEq(args.positional.data.data[i], "--recurse"))
            recurse = true;
    }

    byteview safePath = MakeCStringPath(filePath, alloc);

    struct stat st;
    if (stat((char const *)safePath.data, &st) == 0 && S_ISDIR(st.st_mode))
    {
        dir_iter *iter = DirIter::Create(alloc, safePath);
        if (!iter)
        {
            LOG("ERROR: cannot list directory '%.*s'", (int)filePath.size, filePath.data);
            return;
        }

        file_metadata meta;
        bool first = true;
        while (DirIter::Next(alloc, *iter, meta))
        {
            byteview name = meta.fileName;
            uint64_t fullLen = filePath.size + 1 + name.size;
            span<uint8_t> fullPath = RegionAlloc::AllocArray<uint8_t>(alloc, fullLen + 1);
            MemCpy(fullPath.data, filePath.data, filePath.size);
            fullPath.data[filePath.size] = '/';
            MemCpy(fullPath.data + filePath.size + 1, name.data, name.size);
            fullPath.data[fullLen] = 0;

            byteview childPath{fullPath.data, fullLen};
            if (DetectLanguage(childPath) == source_language::Unknown)
                continue;

            auto blocks = ListBlocks(childPath, alloc);
            if (blocks.size == 0)
                continue;

            if (!first)
                FileWriteFmt(GetStdout(), "\n"_s);
            first = false;

            PrintBlockList(name, blocks);
        }
        DirIter::Destroy(*iter);
        return;
    }

    auto blocks = ListBlocks(safePath, alloc);
    PrintBlockList(filePath, blocks);
}
void CmdMove(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 4)
    {
        LOG("Usage: xcav move <file> <line> <dest-line>");
        LOG("  Moves the structural block containing <line> to after <dest-line>.");
        LOG("  Lines are 1-indexed (as shown by 'xcav blocks').");
        return;
    }

    byteview filePath = args.positional.data.data[1];

    byte_parser lineParser{};
    byte_parser destParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    ByteParser::Init(destParser, args.positional.data.data[3].data, args.positional.data.data[3].size);

    int64_t lineVal = StringParser::ParseLong(lineParser);
    int64_t destVal = StringParser::ParseLong(destParser);

    if (lineVal < 1 || destVal < 1)
    {
        LOG("ERROR: line numbers must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    // Null-terminate the file path for FileOpen (which calls Span::CStr)
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    uint32_t blockLine = (uint32_t)(lineVal - 1);
    uint32_t destLine = (uint32_t)(destVal - 1);

    bool ok = MoveBlock(byteview{pathBuf.data, filePath.size}, blockLine, destLine, alloc);
    if (!ok)
    {
        s_exitCode = 1;
        // MoveBlock already logged specific error
    }
}

void CmdMoveInto(region_alloc &alloc, const cli_args &args)
{
    // Usage: xcav move-into <src-file> <line> <dst-file> <line> [--copy-includes]
    if (args.positional.size < 5)
    {
        LOG("Usage: xcav move-into <src-file> <line> <dst-file> <dst-line> [--copy-includes]");
        LOG("  Moves the structural block at <line> in <src-file> to after <dst-line> in <dst-file>.");
        LOG("  Lines are 1-indexed (as shown by 'xcav blocks').");
        LOG("  --copy-includes: copy #include/import lines from source to destination.");
        return;
    }

    byteview srcFilePath = args.positional.data.data[1];
    byteview dstFilePath = args.positional.data.data[3];

    byte_parser lineParser{};
    byte_parser destParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    ByteParser::Init(destParser, args.positional.data.data[4].data, args.positional.data.data[4].size);

    int64_t srcLineVal = StringParser::ParseLong(lineParser);
    int64_t dstLineVal = StringParser::ParseLong(destParser);

    if (srcLineVal < 1 || dstLineVal < 1)
    {
        LOG("ERROR: line numbers must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    bool copyIncludes = false;
    for (uint64_t i = 5; i < args.positional.size; ++i)
    {
        if (ByteviewEq(args.positional.data.data[i], "--copy-includes"))
            copyIncludes = true;
    }

    // Null-terminate paths
    auto safePath = [&](byteview p) -> byteview {
        span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, p.size + 1);
        MemCpy(buf.data, p.data, p.size);
        buf.data[p.size] = 0;
        return byteview{buf.data, p.size};
    };

    uint32_t srcLine = (uint32_t)(srcLineVal - 1);
    uint32_t dstLine = (uint32_t)(dstLineVal - 1);

    bool ok = MoveBlockInto(safePath(srcFilePath), srcLine, safePath(dstFilePath), dstLine, copyIncludes, alloc);
    if (!ok)
    {
        s_exitCode = 1;
        // MoveBlockInto already logged specific error
    }
}

void CmdDelete(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 3)
    {
        LOG("Usage: xcav delete <file> <line>");
        LOG("  Deletes the structural block containing <line>.");
        LOG("  Lines are 1-indexed (as shown by 'xcav blocks').");
        return;
    }

    byteview filePath = args.positional.data.data[1];

    byte_parser lineParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    int64_t lineVal = StringParser::ParseLong(lineParser);

    if (lineVal < 1)
    {
        LOG("ERROR: line number must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    // Null-terminate the file path for FileOpen (which calls Span::CStr)
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    uint32_t blockLine = (uint32_t)(lineVal - 1);

    bool ok = DeleteBlock(byteview{pathBuf.data, filePath.size}, blockLine, alloc);
    if (!ok)
    {
        s_exitCode = 1;
        // DeleteBlock already logged specific error
    }
}

void CmdEdit(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 2)
    {
        LOG("Usage: xcav edit <file> <old-file> <new-file> [--force] [--dry-run]");
        LOG("       xcav edit <file> --stdin [--force] [--dry-run]");
        LOG("  Replaces oldText (full lines only) with newText in the target file.");
        LOG("  oldText must span complete lines — use 'xcav read --raw' to get exact text.");
        LOG("  --stdin mode reads oldText and newText from stdin, separated by a line");
        LOG("  containing only '---XCAV_EDIT_SEPARATOR---'.");
        LOG("  --force skips tree-sitter validation and re-indentation.");
        LOG("  --dry-run shows what would match without modifying the file.");
        LOG("  --diff (always on) print structural block diff (added/removed/modified blocks).");
        return;
    }

    // Scan for flags first, then collect file path and text file args.
    bool stdinMode = false;
    bool force = false;
    bool dryRun = false;
    bool diff = false;
    byteview filePath{};
    byteview oldFile{};
    byteview newFile{};
    byteview stdinFile{};
    uint8_t stdinPathBuf[128]{};
    uint8_t oldPathBuf[128]{};
    uint8_t newPathBuf[128]{};

    for (uint64_t i = 1; i < args.positional.size; ++i)
    {
        byteview arg = args.positional.data.data[i];
        if (ByteviewEq(arg, "--stdin"))
            stdinMode = true;
        else if (ByteviewEq(arg, "--force"))
            force = true;
        else if (ByteviewEq(arg, "--dry-run"))
            dryRun = true;
        else if (ByteviewEq(arg, "--diff"))
            diff = true;
        else if (filePath.size == 0)
            filePath = arg;
        else if (oldFile.size == 0)
            oldFile = arg;
        else if (newFile.size == 0)
            newFile = arg;
    }

    if (filePath.size == 0)
    {
        LOG("ERROR: missing file path — provide a file to edit");
        return;
    }

    if (!stdinMode && (oldFile.size == 0 || newFile.size == 0))
    {
        LOG("ERROR: expected <old-file> <new-file> or --stdin");
        return;
    }

    if (stdinMode)
    {
        const char *separator = "---XCAV_EDIT_SEPARATOR---";
        uint32_t sepLen = 25;

        uint32_t pid = (uint32_t)getpid();
        stdinFile.size = StringWriteFmt(span<uint8_t>{stdinPathBuf, sizeof(stdinPathBuf) - 1},
                                        "/tmp/xcav_edit_%u_stdin.txt"_s, pid);
        oldFile.size = StringWriteFmt(span<uint8_t>{oldPathBuf, sizeof(oldPathBuf) - 1},
                                      "/tmp/xcav_edit_%u_old.txt"_s, pid);
        newFile.size = StringWriteFmt(span<uint8_t>{newPathBuf, sizeof(newPathBuf) - 1},
                                      "/tmp/xcav_edit_%u_new.txt"_s, pid);
        stdinPathBuf[stdinFile.size] = 0;
        oldPathBuf[oldFile.size] = 0;
        newPathBuf[newFile.size] = 0;
        stdinFile.data = stdinPathBuf;
        oldFile.data = oldPathBuf;
        newFile.data = newPathBuf;

        file_handle stdinOut = FileOpen(stdinFile, FileOpenMode::Write);
        if (!FileValid(stdinOut))
        {
            LOG("ERROR: cannot create stdin temp file");
            return;
        }

        uint8_t chunk[4096];
        uint32_t n;
        file_handle stdinFh = GetStdin();
        while ((n = FileRead(stdinFh, sizeof(chunk), chunk)) > 0)
        {
            uint32_t written = FileWrite(stdinOut, n, chunk);
            if (written != n)
            {
                FileClose(stdinOut);
                LOG("ERROR: short write while buffering stdin");
                return;
            }
        }
        FileClose(stdinOut);

        file_handle stdinIn = FileOpen(stdinFile, FileOpenMode::Read);
        if (!FileValid(stdinIn))
        {
            LOG("ERROR: cannot read stdin temp file");
            return;
        }
        byteview stdinBuf = FileReadFully(alloc, stdinIn);
        FileClose(stdinIn);

        // Find the separator line
        uint32_t sepPos = 0;
        bool found = false;
        for (uint32_t i = 0; i + sepLen <= stdinBuf.size; ++i)
        {
            bool lineStart = (i == 0 || stdinBuf.data[i - 1] == '\n');
            bool lineEnd = (i + sepLen == stdinBuf.size || stdinBuf.data[i + sepLen] == '\n');
            if (lineStart && lineEnd)
            {
                if (MemEq(stdinBuf.data + i, separator, sepLen))
                {
                    sepPos = i;
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            LOG("ERROR: separator line not found in stdin — expected '---XCAV_EDIT_SEPARATOR---'");
            return;
        }

        // oldText is everything before the separator (trailing newline stripped).
        uint32_t oldEnd = sepPos;
        if (oldEnd > 0 && stdinBuf.data[oldEnd - 1] == '\n')
            --oldEnd;

        // newText is everything after the separator line (leading newline stripped)
        uint32_t newStart = sepPos + sepLen;
        if (newStart < stdinBuf.size && stdinBuf.data[newStart] == '\n')
            ++newStart;

        file_handle tmpFh = FileOpen(oldFile, FileOpenMode::Write);
        if (FileValid(tmpFh))
        {
            FileWrite(tmpFh, oldEnd, stdinBuf.data);
            FileClose(tmpFh);
        }

        tmpFh = FileOpen(newFile, FileOpenMode::Write);
        if (FileValid(tmpFh))
        {
            FileWrite(tmpFh, (uint32_t)(stdinBuf.size - newStart), stdinBuf.data + newStart);
            FileClose(tmpFh);
        }
    }

    // Ensure all paths are null-terminated (CopyByteView from arena isn't;
    // FileOpen → CStr asserts null termination).
    auto safePath = [&](byteview p) -> byteview {
        span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, p.size + 1);
        MemCpy(buf.data, p.data, p.size);
        buf.data[p.size] = 0;
        return byteview{buf.data, p.size};
    };
    bool ok = EditSafe(safePath(filePath), safePath(oldFile), safePath(newFile), alloc, force, dryRun, diff);
    if (stdinMode)
    {
        unlink(Span::CStr(stdinFile));
        unlink(Span::CStr(oldFile));
        unlink(Span::CStr(newFile));
    }
    if (!ok)
    {
        s_exitCode = 1;
        // EditSafe already logged a specific error
    }
}

void CmdHelp()
{
    LOG("xcav — structural code-moving tool with tree-sitter validation");
    LOG("");
    LOG("USAGE");
    LOG("  xcav <command> [args...]");
    LOG("");
    LOG("COMMANDS");
    LOG("");
    LOG("  xcav blocks <file|directory>");
    LOG("    List structural blocks in a C/C++/Java/TS/JS source file.");
    LOG("    If <file> is a directory, lists blocks for all source files (non-recursive).");
    LOG("    Each file's blocks are preceded by a header line with the filename.");
    LOG("    Output format: START-END KIND name (compact, one per line).");
    LOG("    Kinds: include, struct, enum, func, decl.");
    LOG("    Comments are excluded. Line numbers are 1-indexed.");
    LOG("    Recurses into namespaces but not classes/structs/enums");
    LOG("    (those are treated as opaque blocks).");
    LOG("    Language is detected by file extension: .c/.h → C, .cc/.cpp/.cxx/.hpp/.hxx/.hh → C++,");
    LOG("    .java → Java, .js/.mjs/.cjs → JavaScript, .ts/.mts/.cts → TypeScript,");
    LOG("    .jsx/.tsx → TSX.");
    LOG("    Examples:");
    LOG("      xcav blocks file.cc         — blocks in a single file");
    LOG("      xcav blocks .               — blocks in all files in current directory");
    LOG("");
    LOG("  xcav move <file> <line> <dest-line>");
    LOG("    Move the structural block containing <line> to after <dest-line>.");
    LOG("    Both line numbers are 1-indexed (as shown by 'xcav blocks').");
    LOG("    The block is re-indented to match the destination's indentation level.");
    LOG("    The block must not contain the destination line.");
    LOG("    Destination must be a block boundary (closing brace, file start/end).");
    LOG("    Picking a line inside a function body inserts the block there.");
    LOG("    Examples:");
    LOG("      xcav move file.cc 45 20   — move block at line 45 to after line 20");
    LOG("      xcav move file.cc 45 1    — move block to top of file");
    LOG("");
    LOG("  xcav move-into <src-file> <src-line> <dst-file> <dst-line> [--copy-includes]");
    LOG("    Move the structural block at <src-line> in <src-file> to after");
    LOG("    <dst-line> in <dst-file>. Both line numbers are 1-indexed.");
    LOG("    Same block-detection and re-indentation rules as 'move'.");
    LOG("    --copy-includes: copy #include/import lines from source to dest");
    LOG("    (deduplicated against existing dest includes).");
    LOG("    Examples:");
    LOG("      xcav move-into a.cc 45 b.cc 20");
    LOG("      xcav move-into a.cc 45 b.cc 20 --copy-includes");
    LOG("");
    LOG("  xcav extract <src-file> <line> <new-file>");
    LOG("    Move the structural block at <line> to a new file.");
    LOG("    For C/C++: adds #pragma once, namespace wrapping, and copies #includes.");
    LOG("    For Java/TS/JS: copies imports. No pragma/namespace generation.");
    LOG("    The block's includes/imports are copied to the new file (deduplicated).");
    LOG("    Example:");
    LOG("      xcav extract file.cc 45 new_feature.h");
    LOG("      xcav extract App.java 10 SubFeature.java");
    LOG("");
    LOG("  xcav inline <file> <line>");
    LOG("    Inline a simple single-return function at <line> into the call site.");
    LOG("    Substitutes parameters with arguments. Saves backup (xcav undo).");
    LOG("    Example:");
    LOG("      xcav inline file.c 12");
    LOG("");
    LOG("  xcav replace-block <file> <line> <new-file>");
    LOG("    Replace the structural block containing <line> with content from <new-file>.");
    LOG("    The new content is re-indented to match the old block's indentation level.");
    LOG("    Atomic operation — no risk of content landing outside namespaces.");
    LOG("    Example:");
    LOG("      xcav replace-block file.cc 32 /tmp/new.txt");
    LOG("");
    LOG("  xcav delete <file> <line>");
    LOG("    Delete the structural block containing <line>.");
    LOG("    <line> is 1-indexed. Cleans up surrounding blank lines");
    LOG("    and orphaned comments (except file-level ones).");
    LOG("    Example:");
    LOG("      xcav delete file.cc 32    — delete the function/struct at line 32");
    LOG("");
    LOG("  xcav read <file> [<line>] [flags]");
    LOG("    Read structural blocks in an agent-friendly format.");
    LOG("    Modes:");
    LOG("      xcav read <file> <line>            — block at line (no numbers)");
    LOG("      xcav read <file> <line> --numbers  — block at line with line numbers");
    LOG("      xcav read <file> <line> --raw      — exact text, no un-indent");
    LOG("      xcav read <file> --name <path>     — find by structural name");
    LOG("      xcav read <file> --all             — dump all blocks");
    LOG("      xcav read <file> --all --numbers   — all blocks with line numbers");
    LOG("      xcav read <file> --offset N --limit M — line range with un-indent");
    LOG("    Output shows a header comment with the structural path");
    LOG("    (e.g. 'Point::GetX') and un-indented code to save tokens.");
    LOG("    --offset/--limit mode reads a line range within a block:");
    LOG("    computes min-indent from visible lines (not whole block), so");
    LOG("    deep slices come out compact. Works with --numbers.");
    LOG("    --name supports suffix matching: 'GetX' matches 'Point::GetX'.");
    LOG("    --raw skips un-indenting — outputs exact text with original indent.");
    LOG("    Examples:");
    LOG("      xcav read file.cc 45                     — function at line 45");
    LOG("      xcav read file.cc 45 --numbers           — same, with line numbers");
    LOG("      xcav read file.cc 45 --raw               — exact text, original indent");
    LOG("      xcav read file.cc --name foo             — find function 'foo'");
    LOG("      xcav read file.cc --all --numbers        — dump entire file structure");
    LOG("      xcav read file.cc --offset 120 --limit 8 — 8 lines starting at line 120, un-indented");
    LOG("      xcav read file.sh --offset 10 --limit 5 --raw — exact text for non-code files");
    LOG("");
    LOG("  xcav replace <file> <line> <old-file> <new-file>");
    LOG("    Replace text within a specific structural block (scope-safe).");
    LOG("    Like 'edit' but scoped to the block containing <line>, so oldText");
    LOG("    doesn't need to be globally unique. No tree-sitter validation.");
    LOG("    Normalizes Unicode punctuation (em-dash → --, arrows → ASCII)");
    LOG("    in both oldText and newText before matching.");
    LOG("    Example:");
    LOG("      xcav replace file.cc 32 /tmp/old.txt /tmp/new.txt");
    LOG("");
    LOG("  xcav undo <file>");
    LOG("    Restore <file> from its most recent backup in .xcav_backups/.");
    LOG("    Backups are created automatically before every move/delete/edit/replace/replace-block/tidy/extract.");
    LOG("    .xcav_backups/.gitignore is created automatically so backup payloads stay out of commits.");
    LOG("    Supports multiple undo levels (one backup per operation version).");
    LOG("    Example:");
    LOG("      xcav undo file.cc          — undo the last mutation on file.cc");
    LOG("");
    LOG("  xcav edit <file> <old-file> <new-file> [--dry-run] [--force]");
    LOG("  xcav edit <file> --stdin [--dry-run] [--force]");
    LOG("    Safe edit with tree-sitter validation (full lines only).");
    LOG("    Replaces oldText (contents of <old-file>) with newText (contents of");
    LOG("    <new-file>) in the target <file>. oldText must span complete lines —");
    LOG("    use 'xcav read <file> <line> --raw' to get the exact text to replace.");
    LOG("    The oldText must be unique in the file. If tree-sitter finds syntax");
    LOG("    errors after the edit, auto-fixes are attempted (re-indentation,");
    LOG("    whitespace cleanup, tab→space normalization, blank line collapse).");
    LOG("    If the result still has errors, the edit is rejected and the file is");
    LOG("    left unchanged.");
    LOG("    Normalizes Unicode punctuation (em-dash → --, arrows → ASCII)");
    LOG("    in both oldText and newText before matching.");
    LOG("    --stdin mode reads oldText and newText from stdin, separated by a");
    LOG("    line containing only '---XCAV_EDIT_SEPARATOR---'.");
    LOG("    --dry-run: validate and report matches, but do not write to file.");
    LOG("    --force: skip tree-sitter validation and re-indentation.");
    LOG("    --diff: (always on) structural block diff is shown automatically.");
    LOG("    Examples:");
    LOG("      xcav edit file.cc /tmp/old.txt /tmp/new.txt");
    LOG("      xcav edit file.cc /tmp/old.txt /tmp/new.txt --diff");
    LOG("      xcav edit file.cc /tmp/old.txt /tmp/new.txt --dry-run");
    LOG("      xcav edit file.cc /tmp/old.txt /tmp/new.txt --force");
    LOG("      echo -e 'old\\n---XCAV_EDIT_SEPARATOR---\\nnew' | xcav edit file.cc --stdin");
    LOG("");
    LOG("  xcav tidy <file>");
    LOG("    Re-indent every structural block in the file.");
    LOG("    Computes correct indentation from tree-sitter nesting depth");
    LOG("    (namespaces → 0, struct/class → +4, enum → +4, function → +8, etc.).");
    LOG("    Also cleans up trailing whitespace, normalizes tabs→spaces,");
    LOG("    and collapses consecutive blank lines.");
    LOG("    Example:");
    LOG("      xcav tidy file.cc");
    LOG("");
    LOG("  xcav onboard");
    LOG("    Print the agent onboarding guide (used by Pi coding agent).");
    LOG("");
    LOG("  xcav help");
    LOG("    Print this help text.");
    LOG("");
    LOG("OUTPUT");
    LOG("  Data (block lists, read code) → stdout.");
    LOG("  Errors and diagnostics → stderr.");
    LOG("");
    LOG("EXIT CODES");
    LOG("  0 — success");
    LOG("  non-zero — assertion failure or operation failed");
    LOG("");
    LOG("LANGUAGES");
    LOG("  C, C++, Java, JavaScript, TypeScript, TSX. Detected by file extension.");
    LOG("  Uses vendored tree-sitter grammars (tolerant of syntax errors).");
}
void CmdUndo(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 2)
    {
        LOG("Usage: xcav undo <file>");
        LOG("  Restores <file> from its most recent .xcav_backups entry.");
        return;
    }

    byteview filePath = args.positional.data.data[1];

    // Null-terminate
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    RestoreBackup(byteview{pathBuf.data, filePath.size}, alloc);
}

void CmdRead(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 2)
    {
        LOG("Usage: xcav read <file> [<line>] [flags]");
        LOG("  xcav read <file> <line>            — read block at line");
        LOG("  xcav read <file> <line> --numbers  — include line numbers");
        LOG("  xcav read <file> <line> --raw      — exact text, no un-indent");
        LOG("  xcav read <file> --name <path>     — find block by structural name");
        LOG("  xcav read <file> --all             — dump all blocks with code");
        LOG("  Flags: --numbers, --raw, --name, --all, --fix");
        LOG("  --fix  write corrected indentation back to disk");
        return;
    }

    byteview filePath = args.positional.data.data[1];

    // Null-terminate
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;
    byteview safePath{pathBuf.data, filePath.size};

    // Parse flags and positional args
    bool showNumbers = false;
    bool allMode = false;
    bool rawMode = false;
    bool fixMode = false;
    byteview nameFilter{};
    int64_t lineVal = 0;
    bool hasLine = false;
    uint32_t offsetVal = 0;
    uint32_t limitVal = 0;
    bool hasOffset = false;
    bool hasLimit = false;

    // args.positional.data[0] = "read", [1] = filePath, rest are flags or line
    for (uint64_t i = 2; i < args.positional.size; ++i)
    {
        byteview arg = args.positional.data.data[i];
        if (ByteviewEq(arg, "--numbers"))
            showNumbers = true;
        else if (ByteviewEq(arg, "--raw"))
            rawMode = true;
        else if (ByteviewEq(arg, "--fix"))
            fixMode = true;
        else if (ByteviewEq(arg, "--all"))
            allMode = true;
        else if (ByteviewEq(arg, "--name"))
        {
            ++i;
            if (i < args.positional.size)
                nameFilter = args.positional.data.data[i];
            else
            {
                s_exitCode = 1;
                LOG("ERROR: --name requires a value — provide a structural name to find");
                return;
            }
        }
        else if (ByteviewEq(arg, "--offset"))
        {
            ++i;
            if (i < args.positional.size)
            {
                byte_parser op{};
                ByteParser::Init(op, args.positional.data.data[i].data, args.positional.data.data[i].size);
                offsetVal = (uint32_t)StringParser::ParseLong(op);
                hasOffset = true;
            }
        }
        else if (ByteviewEq(arg, "--limit"))
        {
            ++i;
            if (i < args.positional.size)
            {
                byte_parser lp2{};
                ByteParser::Init(lp2, args.positional.data.data[i].data, args.positional.data.data[i].size);
                limitVal = (uint32_t)StringParser::ParseLong(lp2);
                hasLimit = true;
            }
        }
        else if (!hasLine)
        {
            byte_parser lp{};
            ByteParser::Init(lp, arg.data, arg.size);
            lineVal = StringParser::ParseLong(lp);
            if (lineVal >= 1)
                hasLine = true;
        }
    }

    // Helper to print a single block
    auto printBlock = [&](const read_block_info &info) {
        // Header
        FileWriteFmt(GetStdout(), "// %.*s (%.*s, lines %u-%u)\n"_s, (int)info.path.size, info.path.data.data,
                     (int)info.type.size, info.type.data.data, info.startLine + 1, info.endLine + 1);

        if (showNumbers)
        {
            uint32_t lineNum = info.startLine + 1;
            uint32_t pos = 0;
            while (pos < info.text.size)
            {
                FileWriteFmt(GetStdout(), "%4u: "_s, lineNum);
                while (pos < info.text.size && info.text.data[pos] != '\n')
                {
                    FileWriteFmt(GetStdout(), "%c"_s, info.text.data[pos]);
                    ++pos;
                }
                FileWriteFmt(GetStdout(), "\n"_s);
                if (pos < info.text.size)
                    ++pos;
                ++lineNum;
            }
        }
        else
        {
            FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)info.text.size, info.text.data);
        }
    };

    // ── --all mode ──
    if (allMode)
    {
        source_language lang = DetectLanguage(safePath);
        if (lang == source_language::Unknown)
        {
            // Unsupported file type — output entire file as one block
            file_handle fh = FileOpen(safePath, FileOpenMode::Read);
            if (!FileValid(fh))
            {
                LOG("ERROR: cannot open file");
                return;
            }
            byteview text = FileReadFully(alloc, fh);
            FileClose(fh);
            if (text.size == 0)
            {
                LOG("ERROR: empty file");
                return;
            }
            FileWriteFmt(GetStdout(), "// %.*s (plain file, %llu bytes)\n"_s, (int)filePath.size,
                         filePath.data, (unsigned long long)text.size);
            if (!rawMode)
            {
                // Un-indent like a normal block
                uint32_t minIndent = 0xFFFFFFFF;
                {
                    uint32_t p = 0;
                    while (p < text.size)
                    {
                        uint32_t indent = 0;
                        while (p + indent < text.size &&
                               (text.data[p + indent] == ' ' || text.data[p + indent] == '\t'))
                            ++indent;
                        bool hasContent = false;
                        uint32_t cs = p + indent;
                        while (cs < text.size && text.data[cs] != '\n')
                        {
                            if (text.data[cs] != ' ' && text.data[cs] != '\t')
                            { hasContent = true; break; }
                            ++cs;
                        }
                        if (hasContent && indent < minIndent)
                            minIndent = indent;
                        while (p < text.size && text.data[p] != '\n')
                            ++p;
                        if (p < text.size)
                            ++p;
                    }
                }
                if (minIndent == 0xFFFFFFFF)
                    minIndent = 0;
                uint32_t p = 0;
                while (p < text.size)
                {
                    uint32_t strip = 0;
                    while (p < text.size && strip < minIndent &&
                           (text.data[p] == ' ' || text.data[p] == '\t'))
                    { ++p; ++strip; }
                    while (p < text.size && text.data[p] != '\n')
                        FileWriteFmt(GetStdout(), "%c"_s, text.data[p++]);
                    FileWriteFmt(GetStdout(), "\n"_s);
                    if (p < text.size)
                        ++p;
                }
            }
            else
            {
                FileWriteFmt(GetStdout(), "%.*s"_s, (int)text.size, text.data);
                if (text.size == 0 || text.data[text.size - 1] != '\n')
                    FileWriteFmt(GetStdout(), "\n"_s);
            }
            return;
        }

        auto blocks = ListBlocks(safePath, alloc);
        if (blocks.size == 0)
        {
            LOG("ERROR: no blocks found");
            return;
        }
        uint64_t printedCount = 0;
        for (uint64_t i = 0; i < blocks.size; ++i)
        {
            // Skip root-level container nodes (translation_unit, program)
            block_info &b = blocks.data.data[i];
            if (b.type.size >= 16 && b.type.data[0] == 't')
                continue; // translation_unit
            if (b.type.size >= 7 && b.type.data[0] == 'p')
                continue; // program

            read_block_info info = ReadBlock(safePath, b.startLine, alloc, rawMode);
                        if (info.text.size > 0 && info.path.size > 0)
                        {
                            if (printedCount > 0)
                                FileWriteFmt(GetStdout(), "\n"_s);
                            printBlock(info);
                ++printedCount;
            }
        }
        if (printedCount == 0)
            LOG("ERROR: no blocks found");
        return;
    }

    // ── --name mode ──
    if (nameFilter.size > 0)
    {
        auto blocks = ListBlocks(safePath, alloc);
        for (uint64_t i = 0; i < blocks.size; ++i)
        {
            read_block_info info = ReadBlock(safePath, blocks.data.data[i].startLine, alloc, rawMode);
                        if (info.text.size > 0)
            {
                // Compare paths: allow partial match (e.g. "GetX" matches "Point::GetX")
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
                    // Check if path ends with "::nameFilter"
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
                    printBlock(info);
                    return;
                }
            }
        }
        s_exitCode = 1;
        LOG("ERROR: no block matching '%.*s'", (int)nameFilter.size, nameFilter.data);
        return;
    }

    // ── Helper: plain file reading (non-code files) ──
    auto printPlainFile = [&](uint32_t startLine, uint32_t maxLines, bool raw) {
        file_handle fh = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh)) { LOG("ERROR: cannot open file"); return; }
        byteview text = FileReadFully(alloc, fh);
        FileClose(fh);
        if (text.size == 0) { LOG("ERROR: empty file"); return; }

        // Split into lines
        inline_vec<byteview, 4096> lines{};
        {
            uint32_t p = 0;
            while (p < text.size)
            {
                uint32_t ls = p;
                while (p < text.size && text.data[p] != '\n') ++p;
                InlineVec::Append(lines, byteview{text.data + ls, p - ls});
                if (p < text.size && text.data[p] == '\n') ++p;
            }
        }

        uint32_t endLine = startLine + maxLines;
        if (endLine > lines.size) endLine = lines.size;
        if (startLine >= lines.size) { LOG("ERROR: offset %u exceeds file length (%llu lines)", startLine + 1, (unsigned long long)lines.size); return; }

        if (raw)
        {
            // --raw: output exact text, no un-indentation
            for (uint32_t li = startLine; li < endLine; ++li)
            {
                byteview line = lines.data.data[li];
                if (showNumbers) FileWriteFmt(GetStdout(), "%4u: "_s, li + 1);
                FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)line.size, line.data);
            }
        }
        else
        {
            // Compute min indent for un-indentation
            uint32_t minIndent = 0xFFFFFFFF;
            for (uint32_t li = startLine; li < endLine; ++li)
            {
                byteview line = lines.data.data[li];
                uint32_t indent = 0;
                while (indent < line.size && (line.data[indent] == ' ' || line.data[indent] == '\t')) ++indent;
                bool hasContent = false;
                for (uint32_t c = indent; c < line.size; ++c)
                    if (line.data[c] != ' ' && line.data[c] != '\t') { hasContent = true; break; }
                if (hasContent && indent < minIndent) minIndent = indent;
            }
            if (minIndent == 0xFFFFFFFF) minIndent = 0;

            for (uint32_t li = startLine; li < endLine; ++li)
            {
                byteview line = lines.data.data[li];
                uint32_t strip = minIndent;
                uint32_t p = 0;
                while (p < line.size && strip > 0 && (line.data[p] == ' ' || line.data[p] == '\t')) { ++p; --strip; }
                if (showNumbers) FileWriteFmt(GetStdout(), "%4u: "_s, li + 1);
                FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)(line.size - p), line.data + p);
            }
        }

        uint64_t totalLines = lines.size;
        bool truncated = (endLine - startLine > 2000) || (text.size > 50000);
        if (truncated)
            FileWriteFmt(GetStdout(), "\n[Truncated: %u lines. Use --offset/--limit to narrow.]\n"_s, endLine - startLine);
    };

    // ── offset mode (slice with re-unindent) ──
    if (hasOffset)
    {
        if (offsetVal < 1)
        {
            s_exitCode = 1;
            LOG("ERROR: --offset must be >= 1");
            return;
        }

        source_language offsetLang = DetectLanguage(safePath);
        if (offsetLang == source_language::Unknown)
        {
            printPlainFile(offsetVal - 1, hasLimit ? limitVal : 0xFFFFFFFFu, rawMode);
            return;
        }

        read_block_info info = ReadBlock(safePath, offsetVal - 1, alloc, rawMode);
        if (info.text.size == 0)
        {
            s_exitCode = 1;
            LOG("ERROR: no block found at offset %u", offsetVal);
            return;
        }

        // Slice the requested line range from the un-indented block text.
        // info.text is already un-indented relative to the block's baseline.
        // startLine is 0-indexed.
        uint32_t sliceStart = offsetVal - 1 - info.startLine;
        uint32_t sliceEnd = sliceStart + (hasLimit ? limitVal : 0xFFFFFFFFu);

        inline_vec<byteview, 256> sliceLines{};
        {
            uint32_t pos = 0;
            uint32_t lineIdx = 0;
            while (pos < info.text.size && lineIdx < sliceEnd)
            {
                uint32_t lineStart = pos;
                while (pos < info.text.size && info.text.data[pos] != '\n')
                    ++pos;
                if (lineIdx >= sliceStart)
                    InlineVec::Append(sliceLines, byteview{info.text.data + lineStart, pos - lineStart});
                if (pos < info.text.size)
                    ++pos; // skip \n
                ++lineIdx;
            }
        }

        // Compute the minimum indent across non-blank lines in the slice.
        uint32_t minIndent = 0xFFFFFFFF;
        for (uint64_t li = 0; li < sliceLines.size; ++li)
        {
            byteview line = sliceLines.data.data[li];
            uint32_t indent = 0;
            while (indent < line.size && (line.data[indent] == ' ' || line.data[indent] == '\t'))
                ++indent;
            bool hasContent = false;
            for (uint32_t c = indent; c < line.size; ++c)
            {
                if (line.data[c] != ' ' && line.data[c] != '\t')
                {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent && indent < minIndent)
                minIndent = indent;
        }
        if (minIndent == 0xFFFFFFFF)
            minIndent = 0;

        // Output with slice-level un-indentation.
        for (uint64_t li = 0; li < sliceLines.size; ++li)
        {
            byteview line = sliceLines.data.data[li];
            uint32_t strip = minIndent;
            uint32_t p = 0;
            while (p < line.size && strip > 0 && (line.data[p] == ' ' || line.data[p] == '\t'))
            {
                ++p;
                --strip;
            }
            if (showNumbers)
                FileWriteFmt(GetStdout(), "%4u: "_s, offsetVal + (uint32_t)li);
            FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)(line.size - p), line.data + p);
        }
        return;
    }

    // ── Helper: write corrected indentation back to disk ──
    auto fixIndent = [&](read_block_info &info) {
        // Read the current file
        file_handle fh2 = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh2))
        {
            s_exitCode = 1;
            LOG("ERROR: cannot open file for reading (--fix mode)");
            return;
        }
        byteview fileContent = FileReadFully(alloc, fh2);
        FileClose(fh2);

        // Check if indent actually changed
        uint32_t origLen = info.endByte - info.startByte;
        if (origLen == info.text.size)
        {
            bool same = true;
            for (uint32_t j = 0; j < origLen; ++j)
                if (fileContent.data[info.startByte + j] != info.text.data[j])
                {
                    same = false;
                    break;
                }
            if (same)
                return; // nothing to fix
        }

        SaveBackup(safePath, alloc);

        // Build new content: before + unindented text + after
        uint64_t newSize = info.startByte + info.text.size + (fileContent.size - info.endByte);
        span<uint8_t> newBuf = RegionAlloc::AllocArray<uint8_t>(alloc, newSize);
        MemCpy(newBuf.data, fileContent.data, info.startByte);
        MemCpy(newBuf.data + info.startByte, info.text.data, info.text.size);
        MemCpy(newBuf.data + info.startByte + info.text.size,
               fileContent.data + info.endByte,
               fileContent.size - info.endByte);

        file_handle fh3 = FileOpen(safePath, FileOpenMode::Write);
        if (!FileValid(fh3))
        {
            s_exitCode = 1;
            LOG("ERROR: cannot open file for writing (--fix mode)");
            return;
        }
        uint32_t written = FileWrite(fh3, newSize, newBuf.data);
        FileClose(fh3);
        if (written != newSize)
            LOG("ERROR: short write (--fix mode) — file may be corrupt");
        else
            LOG("OK: wrote corrected indent to '%.*s'", (int)safePath.size, safePath.data);
    };

    // ── line mode (default) ──
    if (!hasLine)
    {
        s_exitCode = 1;
        LOG("ERROR: expected a line number, --name, --all, or --offset");
        return;
    }

    // --fix forces non-raw mode (we need un-indented text to write back)
    source_language lineLang = DetectLanguage(safePath);
    if (lineLang == source_language::Unknown)
    {
        printPlainFile((uint32_t)(lineVal - 1), 1, fixMode ? false : rawMode);
        return;
    }

    read_block_info info = ReadBlock(safePath, (uint32_t)(lineVal - 1), alloc, fixMode ? false : rawMode);
    if (info.text.size == 0)
    {
        s_exitCode = 1;
        LOG("ERROR: no block found at line %lld — use xcav blocks to browse", (long long)lineVal);
        return;
    }
    if (fixMode)
        fixIndent(info);
    printBlock(info);
}

void CmdReplace(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 5)
    {
        LOG("Usage: xcav replace <file> <line> <old-file> <new-file>");
        LOG("  Replaces oldText with newText within the structural block at <line>.");
        LOG("  Unlike 'edit', the search is scoped to the block, so oldText doesn't");
        LOG("  need to be globally unique. No tree-sitter validation.");
        LOG("  Lines are 1-indexed.");
        return;
    }

    byteview filePath = args.positional.data.data[1];
    byteview oldFile = args.positional.data.data[3];
    byteview newFile = args.positional.data.data[4];

    byte_parser lineParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    int64_t lineVal = StringParser::ParseLong(lineParser);

    if (lineVal < 1)
    {
        LOG("ERROR: line number must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    // Read old/new text from temp files
    file_handle oldFh = FileOpen(oldFile, FileOpenMode::Read);
    if (!FileValid(oldFh))
    {
        LOG("ERROR: cannot read old-text file");
        return;
    }
    byteview oldText = FileReadFully(alloc, oldFh);
    FileClose(oldFh);

    file_handle newFh = FileOpen(newFile, FileOpenMode::Read);
    if (!FileValid(newFh))
    {
        LOG("ERROR: cannot read new-text file");
        return;
    }
    byteview newText = FileReadFully(alloc, newFh);
    FileClose(newFh);

    // Null-terminate file path
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    bool ok = ReplaceInBlock(byteview{pathBuf.data, filePath.size}, (uint32_t)(lineVal - 1), oldText, newText, alloc);
    if (!ok)
    {
        s_exitCode = 1;
        // ReplaceInBlock already logged a specific error
    }
}

void CmdReplaceBlock(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 4)
    {
        LOG("Usage: xcav replace-block <file> <line> <new-file>");
        LOG("  Replaces the structural block containing <line> with content from <new-file>.");
        LOG("  The new content is re-indented to match the old block's indentation.");
        LOG("  Lines are 1-indexed.");
        return;
    }

    byteview filePath = args.positional.data.data[1];
    byteview newFile = args.positional.data.data[3];

    byte_parser lineParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    int64_t lineVal = StringParser::ParseLong(lineParser);

    if (lineVal < 1)
    {
        LOG("ERROR: line number must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    // Null-terminate the file path for FileOpen
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    uint32_t blockLine = (uint32_t)(lineVal - 1);

    bool ok = ReplaceBlock(byteview{pathBuf.data, filePath.size}, blockLine, newFile, alloc);
    if (!ok)
    {
        s_exitCode = 1;
        // ReplaceBlock already logged a specific error
    }
}

void CmdOnboard()
{
    FileWriteFmt(GetStdout(),
#include "xcav/onboard.inl"
    );
}

// ─── Entry point ────────────────────────────────────────────────────────────

void CmdExtract(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 4)
    {
        LOG("Usage: xcav extract <src-file> <line> <new-file>");
        LOG("  Moves the structural block at <line> to a new file.");
        LOG("  Creates <new-file> with #pragma once and namespace wrapper.");
        LOG("  Copies #include lines from source to new file.");
        LOG("  Adds #include for new file in source.");
        LOG("  Lines are 1-indexed (as shown by 'xcav blocks').");
        return;
    }

    byteview srcFilePath = args.positional.data.data[1];
    byteview dstFilePath = args.positional.data.data[3];

    byte_parser lineParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    int64_t lineVal = StringParser::ParseLong(lineParser);

    if (lineVal < 1)
    {
        LOG("ERROR: line number must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    // Null-terminate paths
    auto safePath = [&](byteview p) -> byteview {
        span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, p.size + 1);
        MemCpy(buf.data, p.data, p.size);
        buf.data[p.size] = 0;
        return byteview{buf.data, p.size};
    };

    bool ok = BlockExtract(safePath(srcFilePath), (uint32_t)(lineVal - 1), safePath(dstFilePath), alloc);
    if (!ok)
    {
        s_exitCode = 1;
        // BlockExtract already logged specific error
    }
}

void CmdTidy(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 2)
    {
        LOG("Usage: xcav tidy <file>");
        LOG("  Re-indents every structural block based on tree-sitter nesting depth.");
        LOG("  Also cleans up trailing whitespace, tabs→spaces, blank line collapse.");
        return;
    }

    byteview filePath = args.positional.data.data[1];
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    bool ok = TidyFile(byteview{pathBuf.data, filePath.size}, alloc);
    if (!ok)
    {
                s_exitCode = 1;
                // TidyFile already logged specific error
            }
        }

        // ─── CmdInline ───────────────────────────────────────────────────────────

        void CmdInline(region_alloc &alloc, const cli_args &args)
        {
            if (args.positional.size < 3)
            {
                LOG("Usage: xcav inline <file> <line>");
                return;
            }
            byteview filePath = args.positional.data.data[1];
            byte_parser lp{};
            ByteParser::Init(lp, args.positional.data.data[2].data, args.positional.data.data[2].size);
            int64_t lineVal = StringParser::ParseLong(lp);
            if (lineVal < 1) { LOG("ERROR: line number must be >= 1"); s_exitCode = 1; return; }
            span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
            MemCpy(pathBuf.data, filePath.data, filePath.size);
            pathBuf.data[filePath.size] = 0;
            auto result = InlineFunctionCall(byteview{pathBuf.data, filePath.size}, (uint32_t)lineVal, alloc);
            if (!result.ok) { LOG("ERROR: inline failed -- %s", result.error); s_exitCode = 1; }
            else { LOG("OK: inlined function call at line %lld", (long long)lineVal); }
        }

        void Run(region_alloc &alloc)
{
    cli_args args = ParseArgs(alloc);

    if (args.command.size == 0)
    {
        const char *quotes[] = {
            "  //\\\\  ___\n"
            "  Y  \\\\/_/=|    XCAV\n"
            " _L  ((|_L_|      your agent found the legacy code\n"
            "(/)\\(__(____)     and chose violence\n",
            "  //\\\\  ___\n"
            "  Y  \\\\/_/=|    XCAV\n"
            " _L  ((|_L_|      code refactoring,\n"
            "(/)\\(__(____)     but with property damage\n",
            "  //\\\\  ___\n"
            "  Y  \\\\/_/=|    XCAV\n"
            " _L  ((|_L_|      code refactoring,\n"
            "(/)\\(__(____)     but the git diff has a blast radius\n",
        };
        uint32_t pick = 0;  // deterministic: could be seeded from PID or time
        LOG("%s", quotes[pick]);
        LOG("Usage: xcav <command> [args...]");
        LOG("Commands:");
        LOG("  blocks <file>             List structural blocks");
        LOG("  read <file> <line>        Print block with structural path");
        LOG("  move <file> <line> <to>   Move a block");
        LOG("  move-into <src> <ln> <dst> <ln> [--copy-includes] Cross-file move");
        LOG("  delete <file> <line>      Delete a block");
        LOG("  edit <file> <old> <new>   Safe global edit (tree-sitter validated)");
        LOG("  replace <file> <ln> <o> <n> Scoped replace within a block");
        LOG("  replace-block <file> <ln> <new> Replace block with file content");
        LOG("  undo <file>               Restore from .xcav_backup");
        LOG("  extract <file> <ln> <new>  Move block to new file + #include");
        LOG("  tidy <file>               Re-indent file via tree-sitter");
        LOG("  help                      Print detailed help");
        LOG("  onboard                   Print agent onboarding guide");
        return;
    }

    if (ByteviewEq(args.command, "blocks"))
        CmdBlocks(alloc, args);
    else if (ByteviewEq(args.command, "read"))
        CmdRead(alloc, args);
    else if (ByteviewEq(args.command, "move"))
        CmdMove(alloc, args);
    else if (ByteviewEq(args.command, "move-into"))
        CmdMoveInto(alloc, args);
    else if (ByteviewEq(args.command, "delete"))
        CmdDelete(alloc, args);
    else if (ByteviewEq(args.command, "edit"))
        CmdEdit(alloc, args);
    else if (ByteviewEq(args.command, "replace"))
        CmdReplace(alloc, args);
    else if (ByteviewEq(args.command, "replace-block"))
        CmdReplaceBlock(alloc, args);
    else if (ByteviewEq(args.command, "undo"))
        CmdUndo(alloc, args);
    else if (ByteviewEq(args.command, "extract"))
        CmdExtract(alloc, args);
    else if (ByteviewEq(args.command, "tidy"))
        CmdTidy(alloc, args);
    else if (ByteviewEq(args.command, "inline"))
        CmdInline(alloc, args);
    else if (ByteviewEq(args.command, "help"))
        CmdHelp();
    else if (ByteviewEq(args.command, "onboard"))
        CmdOnboard();
}

} // namespace

void UserMain()
{
    region_alloc alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    Run(alloc);

    if (s_exitCode != 0)
        _exit(s_exitCode);
}

} // namespace nyla
