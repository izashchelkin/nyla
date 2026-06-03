// ─── xcav — code-moving and light structural refactoring tool ───
// Headless CLI app built on nyla_commons_headless (no X11 required at runtime).
//
// Usage:
//   xcav blocks <file>             — list structural blocks in a file
//   xcav move <file> <line> <dest> — move the block at <line> to after <dest>
//   xcav edit <file> <old-file> <new-file>  — safe replace with tree-sitter validation
//
// Uses tree-sitter for structural parsing (tolerates syntax errors).
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

#include <time.h>

#include "xcav/backup.h"
#include "xcav/editor.h"
#include "xcav/insert.h"
#include "xcav/text_util.h"
#include "xcav/usage_log.h"

namespace nyla
{

// Exit code set by commands on failure.
static int s_exitCode = 0;
// Error tag for usage tracking — set by commands on failure.
static const char *s_errorTag = nullptr;

namespace
{

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

static auto MakeCStringPath(byteview path, region_alloc &alloc) -> byteview
{
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, path.size + 1);
    MemCpy(buf.data, path.data, path.size);
    buf.data[path.size] = 0;
    return byteview{buf.data, path.size};
}

static void PrintBlockList(byteview displayPath, inline_vec<block_info, 256> const &blocks)
{
    FileWriteFmt(GetStdout(), "%.*s (%llu blocks)\n"_s, (int)displayPath.size, displayPath.data,
                 (unsigned long long)blocks.size);

    for (uint64_t i = 0; i < blocks.size; ++i)
    {
        block_info const &block = blocks.data.data[i];
        char typeBuf[129];
        uint32_t typeSize = (uint32_t)block.type.size > 128 ? 128 : (uint32_t)block.type.size;
        MemCpy(typeBuf, block.type.data.data, typeSize);
        typeBuf[typeSize] = 0;

        char const *label = BlockTypeLabel(typeBuf);

        // Emit annotation prefix (e.g. "@Override") before the type label.
        // Show signature (method details) when available, else fall back to name.
        byteview displayName = block.signature.size > 0 ? byteview{block.signature.data.data, block.signature.size}
                                                        : byteview{block.name.data.data, block.name.size};

        if (block.annotation.size > 0)
        {
            FileWriteFmt(GetStdout(), "  %4u-%4u %.*s %s %.*s\n"_s, block.startLine + 1, block.endLine + 1,
                         (int)block.annotation.size, block.annotation.data.data, label, (int)displayName.size,
                         displayName.data);
        }
        else if (displayName.size > 0)
            FileWriteFmt(GetStdout(), "  %4u-%4u %s %.*s\n"_s, block.startLine + 1, block.endLine + 1, label,
                         (int)displayName.size, displayName.data);
        else
            FileWriteFmt(GetStdout(), "  %4u-%4u %s\n"_s, block.startLine + 1, block.endLine + 1, label);
    }
}
static void ShowFileBlocks(byteview filePath, region_alloc &alloc)
{
    byteview safePath = MakeCStringPath(filePath, alloc);
    auto blocks = ListBlocks(safePath, alloc);
    if (blocks.size > 0)
        PrintBlockList(filePath, blocks);
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

    if (IsDirectory(safePath))
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
        s_exitCode = 1;
        s_errorTag = "bad_args";
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
        s_errorTag = "move_failed";
    }
    else
    {
        ShowFileBlocks(filePath, alloc);
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
        s_exitCode = 1;
        s_errorTag = "bad_args";
        LOG("ERROR: line numbers must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    bool copyIncludes = false;
    for (uint64_t i = 5; i < args.positional.size; ++i)
    {
        if (ByteviewEq(args.positional.data.data[i], "--copy-includes"))
            copyIncludes = true;
    }

    uint32_t srcLine = (uint32_t)(srcLineVal - 1);
    uint32_t dstLine = (uint32_t)(dstLineVal - 1);

    bool ok = MoveBlockInto(MakeCStringPath(srcFilePath, alloc), srcLine, MakeCStringPath(dstFilePath, alloc), dstLine,
                            copyIncludes, alloc);
    if (!ok)
    {
        s_errorTag = "move_into_failed";
    }
    else
    {
        ShowFileBlocks(srcFilePath, alloc);
        ShowFileBlocks(dstFilePath, alloc);
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
        s_exitCode = 1;
        s_errorTag = "bad_args";
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
        s_errorTag = "delete_failed";
    }
    else
    {
        ShowFileBlocks(filePath, alloc);
    }
}
void CmdEdit(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 2)
    {
        LOG("Usage: xcav edit <file> <old-file> <new-file> [<old-file2> <new-file2> ...] [--dry-run] [--no-blocks]");
        LOG("       xcav edit <file> --stdin [--dry-run] [--no-blocks]");
        LOG("  Replaces oldText with newText using line-based matching.");
        LOG("  Multiple old/new pairs are processed sequentially in one invocation;");
        LOG("  the block list is printed only once after all edits complete.");
        LOG("  Lines are matched by content (whitespace ignored). The indentation");
        LOG("  from the matched lines is copied to the replacement text.");
        LOG("  Use 'xcav read <file> <line>' to get oldText -- the un-indented");
        LOG("  output works directly as oldText input.");
        LOG("  --stdin mode reads oldText then newText from stdin, separated by");
        LOG("  a line containing only '---XCAV_EDIT_SEPARATOR---' (single pair only).");
        LOG("  --dry-run shows what would match without modifying the file.");
        LOG("  --no-blocks suppresses the final block list (for multi-edit callers).");
        return;
    }

    // Scan for flags first, then collect file path and old/new file pairs.
    bool stdinMode = false;
    bool dryRun = false;
    bool noBlocks = false;
    byteview filePath{};
    inline_vec<byteview, 16> oldFiles{};
    inline_vec<byteview, 16> newFiles{};
    byteview stdinFile{};
    uint8_t stdinPathBuf[128]{};
    uint8_t oldPathBuf[128]{};
    uint8_t newPathBuf[128]{};

    for (uint64_t i = 1; i < args.positional.size; ++i)
    {
        byteview arg = args.positional.data.data[i];
        if (ByteviewEq(arg, "--stdin"))
            stdinMode = true;
        else if (ByteviewEq(arg, "--dry-run"))
            dryRun = true;
        else if (ByteviewEq(arg, "--no-blocks"))
            noBlocks = true;
        else if (filePath.size == 0)
            filePath = arg;
        else if (oldFiles.size == newFiles.size)
            InlineVec::Append(oldFiles, arg);
        else
            InlineVec::Append(newFiles, arg);
    }

    if (filePath.size == 0)
    {
        s_exitCode = 1;
        s_errorTag = "bad_args";
        LOG("ERROR: missing file path -- provide a file to edit");
        return;
    }

    if (stdinMode)
    {
        if (oldFiles.size > 0)
        {
            s_exitCode = 1;
            s_errorTag = "bad_args";
            LOG("ERROR: --stdin does not support multiple pairs");
            return;
        }

        const char *separator = "---XCAV_EDIT_SEPARATOR---";
        uint32_t sepLen = 25;

        uint32_t pid = GetProcessId();
        stdinFile.size =
            StringWriteFmt(span<uint8_t>{stdinPathBuf, sizeof(stdinPathBuf) - 1}, "/tmp/xcav_edit_%u_stdin.txt"_s, pid);
        byteview oldFile;
        byteview newFile;
        oldFile.size =
            StringWriteFmt(span<uint8_t>{oldPathBuf, sizeof(oldPathBuf) - 1}, "/tmp/xcav_edit_%u_old.txt"_s, pid);
        newFile.size =
            StringWriteFmt(span<uint8_t>{newPathBuf, sizeof(newPathBuf) - 1}, "/tmp/xcav_edit_%u_new.txt"_s, pid);
        stdinPathBuf[stdinFile.size] = 0;
        oldPathBuf[oldFile.size] = 0;
        newPathBuf[newFile.size] = 0;
        stdinFile.data = stdinPathBuf;
        oldFile.data = oldPathBuf;
        newFile.data = newPathBuf;

        file_handle stdinOut = FileOpen(stdinFile, FileOpenMode::Write);
        if (!FileValid(stdinOut))
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
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
                s_exitCode = 1;
                s_errorTag = "short_write";
                LOG("ERROR: short write while buffering stdin");
                return;
            }
        }
        FileClose(stdinOut);

        file_handle stdinIn = FileOpen(stdinFile, FileOpenMode::Read);
        if (!FileValid(stdinIn))
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
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
            s_exitCode = 1;
            s_errorTag = "bad_args";
            LOG("ERROR: separator line not found in stdin -- expected '---XCAV_EDIT_SEPARATOR---'");
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

        InlineVec::Append(oldFiles, oldFile);
        InlineVec::Append(newFiles, newFile);
    }

    if (oldFiles.size == 0)
    {
        s_exitCode = 1;
        s_errorTag = "bad_args";
        LOG("ERROR: expected <old-file> <new-file> pairs or --stdin");
        return;
    }

    if (oldFiles.size != newFiles.size)
    {
        s_exitCode = 1;
        s_errorTag = "bad_args";
        LOG("ERROR: expected <old-file> <new-file> pairs — uneven count (got %zu old, %zu new)", (size_t)oldFiles.size,
            (size_t)newFiles.size);
        return;
    }

    // Process each old/new pair sequentially.
    uint32_t okCount = 0;
    for (uint64_t i = 0; i < oldFiles.size; ++i)
    {
        bool ok = EditSafe(MakeCStringPath(filePath, alloc), MakeCStringPath(oldFiles.data.data[i], alloc),
                           MakeCStringPath(newFiles.data.data[i], alloc), alloc, dryRun);
        if (stdinMode)
        {
            FileDelete(stdinFile);
            FileDelete(oldFiles.data.data[i]);
            FileDelete(newFiles.data.data[i]);
        }
        if (!ok)
        {
            s_errorTag = "edit_failed";
            s_exitCode = 1;
            return;
        }
        ++okCount;
    }

    if (!noBlocks)
        ShowFileBlocks(filePath, alloc);
}
void CmdHelp()
{
    LOG("xcav — structural code mover for agents");
    LOG("");
    LOG("USAGE");
    LOG("  xcav <command> [args...]");
    LOG("");
    LOG("CORE WORKFLOWS");
    LOG("");
    LOG("  Survey → Read → Edit");
    LOG("    xcav blocks <file>              # see what's there");
    LOG("    xcav read <file> <line>         # get code (un-indented, ready for edit)");
    LOG("    xcav edit <file> <old> <new>    # replace lines (accepts xcav_read output)");
    LOG("");
    LOG("  Restructure within a file");
    LOG("    xcav blocks <file>              # survey");
    LOG("    xcav move <file> <line> <dest>  # move block");
    LOG("    xcav delete <file> <line>       # delete block");
    LOG("    xcav undo <file>                # recover");
    LOG("");
    LOG("  Restructure across files");
    LOG("    xcav blocks <src>; xcav blocks <dst>     # survey both");
    LOG("    xcav move-into <src> <ln> <dst> <ln>     # cross-file move");
    LOG("    xcav copy <src> <ln> <dst> <ln>          # cross-file copy");
    LOG("");
    LOG("  Extract to new file (copy + delete)");
    LOG("    xcav copy <src> <ln> <new> 0             # copy to new file");
    LOG("    xcav delete <src> <ln>                   # remove from source");
    LOG("");
    LOG("  Replace a block");
    LOG("    xcav replace <file> <ln> <old> <new>     # scoped within block");
    LOG("    xcav replace-block <file> <ln> [<new>]  # replace entire block (stdin by default)");
    LOG("");
    LOG("COMMANDS");
    LOG("");
    LOG("  xcav blocks <file|directory>");
    LOG("    List structural blocks with 1-indexed line ranges, types, and names.");
    LOG("    Directory mode: lists blocks for all source files (non-recursive).");
    LOG("    Block types: func, struct, class, enum, constructor, decl, namespace, template,");
    LOG("    interface, export, var, and more.");
    LOG("    Java: annotations (@Override) shown inline, method signatures include");
    LOG("    return type, modifiers, param types+names, and throws clause.");
    LOG("    Examples:");
    LOG("      xcav blocks file.cc");
    LOG("      xcav blocks .                    # current directory");
    LOG("");
    LOG("  xcav read <file> [<line>] [flags]");
    LOG("    Read code in agent-friendly format — un-indented, annotates the");
    LOG("    structural path. Output is directly usable as oldText for xcav edit.");
    LOG("      xcav read <file> <line>            — block at line");
    LOG("      xcav read <file> <line> --numbers  — with line numbers");
    LOG("      xcav read <file> --name <path>     — find by structural name");
    LOG("      xcav read <file> --all             — dump all blocks");
    LOG("      xcav read <file> --offset N --limit M -- line range, un-indented");
    LOG("      xcav read <file> --offset N -- from line N to end of file");
    LOG("    --name supports suffix matching: 'GetX' matches 'Point::GetX'.");
    LOG("    --raw: output exact text with original indentation.");
    LOG("    Non-code files default to printing the whole file.");
    LOG("");
    LOG("  xcav edit <file> <old-file> <new-file> [--dry-run]");
    LOG("  xcav edit <file> --stdin [--dry-run]");
    LOG("    Line-based replacement. Matches oldText to the file by comparing");
    LOG("    line content (ignoring leading/trailing whitespace per line).");
    LOG("    Copies indentation from the matched lines to the replacement text.");
    LOG("    Accepts 'xcav read' output directly as oldText.");
    LOG("    --stdin: read oldText then newText from stdin, separated by");
    LOG("            a line containing '---XCAV_EDIT_SEPARATOR---'.");
    LOG("    --dry-run: report what would match without modifying the file.");
    LOG("");
    LOG("  xcav move <file> <line> <dest-line>");
    LOG("    Move the structural block containing <line> to after <dest-line>.");
    LOG("    Re-indents to match destination. Dest must be a block boundary.");
    LOG("");
    LOG("  xcav move-into <src> <src-line> <dst> <dst-line> [--copy-includes]");
    LOG("    Cross-file move. --copy-includes copies #include/import lines.");
    LOG("    The 'static' keyword is auto-stripped from moved functions.");
    LOG("");
    LOG("  xcav delete <file> <line>");
    LOG("    Delete the structural block at <line>. Cleans up blank lines,");
    LOG("    orphaned comments, and trailing semicolons from type declarations.");
    LOG("");
    LOG("  xcav replace <file> <line> <old-file> <new-file>");
    LOG("    Scoped replace — oldText only needs to be unique within the block");
    LOG("    containing <line>. No tree-sitter validation.");
    LOG("");
    LOG("  xcav replace-block <file> <line> [<new-file>]");
    LOG("    Replace the entire structural block at <line> with new content.");
    LOG("    Without <new-file>, reads replacement block body from stdin.");
    LOG("    The new content is re-indented to match the old block's indentation.");
    LOG("    Atomic -- safe for last block in a namespace (no trailing-brace issues).");
    LOG("");
    LOG("  xcav copy <src> <src-line> <dst> <dst-line> [--copy-includes] [--show-returns]");
    LOG("    Copy a block cross-file. Source is unaffected.");
    LOG("    --copy-includes: copy #include/import lines from source to destination.");
    LOG("    --show-returns: print line numbers of return statements in the copy.");
    LOG("");
    LOG("  xcav insert --before <file> <line> <content-file>");
    LOG("  xcav insert --after  <file> <line> <content-file>");
    LOG("  xcav insert before   <file> <line> <content-file>");
    LOG("  xcav insert after    <file> <line> <content-file>");
    LOG("    Insert code before or after the structural block at <line>.");
    LOG("    Content is read from <content-file> and re-indented to match.");
    LOG("");
    LOG("  xcav undo <file>");
    LOG("    Restore from most recent backup. Multi-level (up to 20).");
    LOG("    Backups created automatically on every mutation.");
    LOG("");
    LOG("  xcav help / xcav onboard");
    LOG("    Print help text or agent onboarding guide.");
    LOG("    'help' is for human consumption; 'onboard' is agent-optimized.");
    LOG("");
    LOG("UNICODE NORMALIZATION");
    LOG("  xcav edit normalizes Unicode: em-dash→'--', arrows→'->'/'<-',");
    LOG("  smart quotes→ASCII. Prevents 'oldText not found' from LLM output.");
    LOG("");
    LOG("LANGUAGES");
    LOG("  C (.c, .h), C++ (.cc, .cpp, .cxx, .hpp, .hxx, .hh), Java (.java),");
    LOG("  JavaScript (.js, .mjs, .cjs), TypeScript (.ts, .mts, .cts),");
    LOG("  TSX (.tsx, .jsx). Detected by file extension. Tree-sitter parsers");
    LOG("  are tolerant of syntax errors.");
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

    if (!RestoreBackup(byteview{pathBuf.data, filePath.size}, alloc))
    {
        s_exitCode = 1;
        s_errorTag = "undo_failed";
    }
    ShowFileBlocks(filePath, alloc);
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
        LOG("  xcav read <file> --all             — print entire file (un-indented)");
        LOG("  xcav read <file> --offset N --limit M — line range, falls back to plain");
        LOG("                                         reading for non-block regions");
        LOG("  xcav read <file>                   — print entire file (un-indented)");
        LOG("  Flags: --numbers, --raw, --name, --all, --offset, --limit, --fix");
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
                s_errorTag = "bad_args";
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

    // ── Default / --all mode: print entire file ──
    if (allMode || (nameFilter.size == 0 && !hasOffset && !hasLine))
    {
        file_handle fh = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh))
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
            LOG("ERROR: cannot open file");
            return;
        }
        byteview text = FileReadFully(alloc, fh);
        FileClose(fh);
        if (text.size == 0)
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
            LOG("ERROR: empty file");
            return;
        }
        // Split into lines
        inline_vec<byteview, 4096> lines{};
        {
            uint32_t p = 0;
            while (p < text.size)
            {
                uint32_t ls = p;
                while (p < text.size && text.data[p] != '\n')
                    ++p;
                InlineVec::Append(lines, byteview{text.data + ls, p - ls});
                if (p < text.size && text.data[p] == '\n')
                    ++p;
            }
        }

        // Large file prevention: truncate to kMaxLines with a warning
        const uint32_t kMaxLines = 500;
        const uint64_t kMaxBytes = 50000;
        uint32_t outputLines = lines.size;
        bool truncated = false;
        if (lines.size > kMaxLines || text.size > kMaxBytes)
        {
            outputLines = kMaxLines;
            truncated = true;
        }

        if (rawMode)
        {
            // Exact output, no un-indent
            uint32_t byteEnd = 0;
            for (uint32_t li = 0; li < outputLines; ++li)
            {
                if (showNumbers)
                    FileWriteFmt(GetStdout(), "%4u: "_s, li + 1);
                byteview line = lines.data.data[li];
                FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)line.size, line.data);
            }
        }
        else
        {
            // Un-indent: find minimum leading whitespace across non-blank lines
            uint32_t minIndent = 0xFFFFFFFF;
            for (uint32_t li = 0; li < outputLines; ++li)
            {
                byteview line = lines.data.data[li];
                uint32_t indent = 0;
                while (indent < line.size && (line.data[indent] == ' ' || line.data[indent] == '\t'))
                    ++indent;
                bool hasContent = false;
                for (uint32_t c = indent; c < line.size; ++c)
                    if (line.data[c] != ' ' && line.data[c] != '\t')
                    {
                        hasContent = true;
                        break;
                    }
                if (hasContent && indent < minIndent)
                    minIndent = indent;
            }
            if (minIndent == 0xFFFFFFFF)
                minIndent = 0;

            for (uint32_t li = 0; li < outputLines; ++li)
            {
                byteview line = lines.data.data[li];
                uint32_t strip = minIndent;
                uint32_t p = 0;
                while (p < line.size && strip > 0 && (line.data[p] == ' ' || line.data[p] == '\t'))
                {
                    ++p;
                    --strip;
                }
                if (showNumbers)
                    FileWriteFmt(GetStdout(), "%4u: "_s, li + 1);
                FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)(line.size - p), line.data + p);
            }
        }

        if (truncated)
            FileWriteFmt(GetStdout(),
                         "\n[Truncated to first %u of %llu lines (%llu bytes). Use --offset/--limit to read more.]\n"_s,
                         kMaxLines, (unsigned long long)lines.size, (unsigned long long)text.size);
        return;
    }
    // ── --name mode ──
    if (nameFilter.size > 0)
    {
        auto blocks = ListBlocks(safePath, alloc);
        read_block_info firstMatch{};
        uint32_t matchCount = 0;
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
                    ++matchCount;
                    if (matchCount == 1)
                    {
                        firstMatch = info;
                    }
                    else
                    {
                        // When names collide (e.g. class and constructor share a name),
                        // prefer container types (class, struct, enum, interface) over
                        // methods/constructors.
                        auto isContainer = [](const read_block_info &ri) -> bool {
                            char typeBuf[128];
                            uint32_t ts = ri.type.size > 127 ? 127 : (uint32_t)ri.type.size;
                            MemCpy(typeBuf, ri.type.data.data, ts);
                            typeBuf[ts] = 0;
                            return StrEq(typeBuf, "class_declaration") || StrEq(typeBuf, "class_specifier") ||
                                   StrEq(typeBuf, "struct_specifier") || StrEq(typeBuf, "interface_declaration") ||
                                   StrEq(typeBuf, "enum_declaration") || StrEq(typeBuf, "enum_specifier");
                        };
                        if (isContainer(info) && !isContainer(firstMatch))
                            firstMatch = info;
                    }
                }
            }
        }
        if (matchCount == 0)
        {
            s_errorTag = "block_not_found";
            s_exitCode = 1;
            LOG("ERROR: no block matching '%.*s'", (int)nameFilter.size, nameFilter.data);
            return;
        }
        if (matchCount > 1)
            LOG("WARNING: --name '%.*s' matches %u blocks (using first match)", (int)nameFilter.size, nameFilter.data,
                matchCount);
        printBlock(firstMatch);
        return;
    }

    // ── Helper: plain file reading (non-code files) ──
    auto printPlainFile = [&](uint32_t startLine, uint32_t maxLines, bool raw) {
        file_handle fh = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh))
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
            LOG("ERROR: cannot open file");
            return;
        }
        byteview text = FileReadFully(alloc, fh);
        FileClose(fh);
        if (text.size == 0)
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
            LOG("ERROR: empty file");
            return;
        }

        // Split into lines
        inline_vec<byteview, 4096> lines{};
        {
            uint32_t p = 0;
            while (p < text.size)
            {
                uint32_t ls = p;
                while (p < text.size && text.data[p] != '\n')
                    ++p;
                InlineVec::Append(lines, byteview{text.data + ls, p - ls});
                if (p < text.size && text.data[p] == '\n')
                    ++p;
            }
        }

        uint32_t endLine = startLine + maxLines;
        if (endLine > lines.size || endLine < startLine)
            endLine = lines.size;
        if (startLine >= lines.size)
        {
            s_exitCode = 1;
            s_errorTag = "bad_args";
            LOG("ERROR: offset %u exceeds file length (%llu lines)", startLine + 1, (unsigned long long)lines.size);
            return;
        }
        if (raw)
        {
            // --raw: output exact text, no un-indentation
            for (uint32_t li = startLine; li < endLine; ++li)
            {
                byteview line = lines.data.data[li];
                if (showNumbers)
                    FileWriteFmt(GetStdout(), "%4u: "_s, li + 1);
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
                while (indent < line.size && (line.data[indent] == ' ' || line.data[indent] == '\t'))
                    ++indent;
                bool hasContent = false;
                for (uint32_t c = indent; c < line.size; ++c)
                    if (line.data[c] != ' ' && line.data[c] != '\t')
                    {
                        hasContent = true;
                        break;
                    }
                if (hasContent && indent < minIndent)
                    minIndent = indent;
            }
            if (minIndent == 0xFFFFFFFF)
                minIndent = 0;

            for (uint32_t li = startLine; li < endLine; ++li)
            {
                byteview line = lines.data.data[li];
                uint32_t strip = minIndent;
                uint32_t p = 0;
                while (p < line.size && strip > 0 && (line.data[p] == ' ' || line.data[p] == '\t'))
                {
                    ++p;
                    --strip;
                }
                if (showNumbers)
                    FileWriteFmt(GetStdout(), "%4u: "_s, li + 1);
                FileWriteFmt(GetStdout(), "%.*s\n"_s, (int)(line.size - p), line.data + p);
            }
        }

        uint64_t totalLines = lines.size;
        bool truncated = (endLine - startLine > 2000) || (text.size > 50000);
        if (truncated)
            FileWriteFmt(GetStdout(), "\n[Truncated: %u lines. Use --offset/--limit to narrow.]\n"_s,
                         endLine - startLine);
        else if (!raw)
            FileWriteFmt(GetStdout(), "\n[Lines %u-%u of %llu. Next: --offset %u]\n"_s, startLine + 1, endLine,
                         (unsigned long long)totalLines, endLine + 1);
    };
    // ── offset mode (slice with re-unindent) ──
    if (hasOffset)
    {
        if (offsetVal < 1)
        {
            s_errorTag = "bad_args";
            s_exitCode = 1;
            LOG("ERROR: --offset must be >= 1");
            return;
        }

        source_language offsetLang = DetectLanguage(safePath);
        if (offsetLang == source_language::Unknown)
        {
            printPlainFile(offsetVal - 1, hasLimit ? limitVal : 0x100000u, rawMode);
            return;
        }

        read_block_info info = ReadBlock(safePath, offsetVal - 1, alloc, rawMode);
        if (info.text.size == 0 || offsetVal - 1 < info.startLine || offsetVal - 1 > info.endLine)
        {
            // No structural block at this offset, or offset is between blocks —
            // fall back to plain line reading.
            printPlainFile(offsetVal - 1, hasLimit ? limitVal : 0x100000u, rawMode);
            return;
        }

        // Slice the requested line range from the un-indented block text.
        // info.text is already un-indented relative to the block's baseline.
        // startLine is 0-indexed.
        uint32_t sliceStart = offsetVal - 1 - info.startLine;
        uint32_t sliceEnd = sliceStart + (hasLimit ? limitVal : 0x100000u);
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
        if (sliceLines.size > 0 && !rawMode)
            FileWriteFmt(GetStdout(), "\n[Lines %u-%u of %u. Next: --offset %u]\n"_s, offsetVal,
                         offsetVal + (uint32_t)sliceLines.size - 1, info.totalLines,
                         offsetVal + (uint32_t)sliceLines.size);
        return;
    }
    // ── Helper: write corrected indentation back to disk ──
    auto fixIndent = [&](read_block_info &info) {
        // Read the current file
        file_handle fh2 = FileOpen(safePath, FileOpenMode::Read);
        if (!FileValid(fh2))
        {
            s_errorTag = "file_error";
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
        MemCpy(newBuf.data + info.startByte + info.text.size, fileContent.data + info.endByte,
               fileContent.size - info.endByte);

        file_handle fh3 = FileOpen(safePath, FileOpenMode::Write);
        if (!FileValid(fh3))
        {
            s_errorTag = "file_error";
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
    // Should not be reached without a line -- defaults are handled above.
    if (!hasLine)
    {
        s_errorTag = "bad_args";
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
        s_errorTag = "block_not_found";
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
        s_errorTag = "bad_args";
        s_exitCode = 1;
        LOG("ERROR: line number must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    // Read old/new text from temp files
    file_handle oldFh = FileOpen(oldFile, FileOpenMode::Read);
    if (!FileValid(oldFh))
    {
        s_errorTag = "file_error";
        s_exitCode = 1;
        LOG("ERROR: cannot read old-text file");
        return;
    }
    byteview oldText = FileReadFully(alloc, oldFh);
    FileClose(oldFh);

    file_handle newFh = FileOpen(newFile, FileOpenMode::Read);
    if (!FileValid(newFh))
    {
        s_errorTag = "file_error";
        s_exitCode = 1;
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
        s_errorTag = "replace_scoped_failed";
    }
    else
    {
        ShowFileBlocks(filePath, alloc);
    }
}
void CmdReplaceBlock(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 3)
    {
        LOG("Usage: xcav replace-block <file> <line> [<new-file>]");
        LOG("  Replaces the structural block containing <line> with new content.");
        LOG("  Without <new-file>, reads replacement block body from stdin.");
        LOG("  The new content is re-indented to match the old block's indentation.");
        LOG("  Lines are 1-indexed.");
        return;
    }

    byteview filePath = args.positional.data.data[1];

    byte_parser lineParser{};
    ByteParser::Init(lineParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    int64_t lineVal = StringParser::ParseLong(lineParser);

    if (lineVal < 1)
    {
        s_errorTag = "bad_args";
        s_exitCode = 1;
        LOG("ERROR: line number must be >= 1 — use 'xcav blocks' to see line numbers");
        return;
    }

    bool stdinMode = (args.positional.size < 4);
    byteview newFile{};
    uint8_t tmpPathBuf[128]{};
    if (stdinMode)
    {
        // Write stdin to a temp file
        uint32_t pid = GetProcessId();
        newFile.size =
            StringWriteFmt(span<uint8_t>{tmpPathBuf, sizeof(tmpPathBuf) - 1}, "/tmp/xcav_replace_%u.txt"_s, pid);
        tmpPathBuf[newFile.size] = 0;
        newFile.data = tmpPathBuf;

        file_handle tmpFh = FileOpen(newFile, FileOpenMode::Write);
        if (!FileValid(tmpFh))
        {
            s_exitCode = 1;
            s_errorTag = "file_error";
            LOG("ERROR: cannot create stdin temp file");
            return;
        }

        uint8_t chunk[4096];
        uint32_t n;
        bool hasContent = false;
        file_handle stdinFh = GetStdin();
        while ((n = FileRead(stdinFh, sizeof(chunk), chunk)) > 0)
        {
            hasContent = true;
            uint32_t written = FileWrite(tmpFh, n, chunk);
            if (written != n)
            {
                FileClose(tmpFh);
                FileDelete(newFile);
                s_exitCode = 1;
                s_errorTag = "short_write";
                LOG("ERROR: short write while buffering stdin");
                return;
            }
        }
        FileClose(tmpFh);

        if (!hasContent)
        {
            FileDelete(newFile);
            s_exitCode = 1;
            s_errorTag = "bad_args";
            LOG("ERROR: stdin is empty — provide replacement block content");
            return;
        }
    }
    else
    {
        newFile = args.positional.data.data[3];
    }

    // Null-terminate the file path for FileOpen
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;

    uint32_t blockLine = (uint32_t)(lineVal - 1);

    bool ok = ReplaceBlock(byteview{pathBuf.data, filePath.size}, blockLine, newFile, alloc);
    if (!ok)
    {
        s_errorTag = "replace_block_failed";
    }
    else
    {
        ShowFileBlocks(filePath, alloc);
    }

    if (stdinMode)
        FileDelete(newFile);
}
void CmdInsert(region_alloc &alloc, const cli_args &args)
{
    // Parse flags and positional args.
    // Supports: insert --before <file> <line> <content>
    //           insert before  <file> <line> <content>
    //           insert --after  <file> <line> <content>
    //           insert after   <file> <line> <content>
    bool before = false;
    bool after = false;
    uint64_t fileIdx = 0;
    uint64_t lineIdx = 0;
    uint64_t contentIdx = 0;

    for (uint64_t i = 1; i < args.positional.size; ++i)
    {
        byteview arg = args.positional.data.data[i];
        if (ByteviewEq(arg, "--before") || ByteviewEq(arg, "before"))
            before = true;
        else if (ByteviewEq(arg, "--after") || ByteviewEq(arg, "after"))
            after = true;
        else if (fileIdx == 0)
            fileIdx = i;
        else if (lineIdx == 0)
            lineIdx = i;
        else if (contentIdx == 0)
            contentIdx = i;
    }

    if (!before && !after)
    {
        s_errorTag = "bad_args";
        s_exitCode = 1;
        LOG("ERROR: specify --before, --after, before, or after");
        LOG("Usage: xcav insert --before|before|--after|after <file> <line> <content-file>");
        return;
    }
    if (before && after)
    {
        s_errorTag = "bad_args";
        s_exitCode = 1;
        LOG("ERROR: specify --before/before or --after/after, not both");
        return;
    }
    if (fileIdx == 0 || lineIdx == 0 || contentIdx == 0)
    {
        s_errorTag = "bad_args";
        s_exitCode = 1;
        LOG("ERROR: expected <file> <line> <content-file>");
        LOG("Usage: xcav insert --before|before|--after|after <file> <line> <content-file>");
        return;
    }

    byteview filePath = args.positional.data.data[fileIdx];
    byteview contentFile = args.positional.data.data[contentIdx];

    byte_parser lineParser{};
    ByteParser::Init(lineParser, args.positional.data.data[lineIdx].data, args.positional.data.data[lineIdx].size);
    int64_t lineVal = StringParser::ParseLong(lineParser);

    if (lineVal < 1)
    {
        s_errorTag = "bad_args";
        s_exitCode = 1;
        LOG("ERROR: line number must be >= 1");
        return;
    }

    uint32_t blockLine = (uint32_t)(lineVal - 1);

    bool ok =
        before
            ? InsertBeforeBlock(MakeCStringPath(filePath, alloc), blockLine, MakeCStringPath(contentFile, alloc), alloc)
            : InsertAfterBlock(MakeCStringPath(filePath, alloc), blockLine, MakeCStringPath(contentFile, alloc), alloc);
    if (!ok)
    {
        s_errorTag = "insert_failed";
        s_exitCode = 1;
    }
    else
    {
        ShowFileBlocks(filePath, alloc);
    }
}
void CmdOnboard()
{
    FileWriteFmt(GetStdout(),
#include "xcav/onboard.inl"
    );
}

// ─── Entry point ────────────────────────────────────────────────────────────

// ─── CmdCopy ────────────────────────────────────────────────────────────

void CmdCopy(region_alloc &alloc, const cli_args &args)
{
    if (args.positional.size < 5)
    {
        LOG("Usage: xcav copy <src-file> <src-line> <dst-file> <dst-line> [--copy-includes] [--show-returns]");
        return;
    }

    byteview srcFilePath = args.positional.data.data[1];
    byteview dstFilePath = args.positional.data.data[3];

    byte_parser srcParser{};
    byte_parser dstParser{};
    ByteParser::Init(srcParser, args.positional.data.data[2].data, args.positional.data.data[2].size);
    ByteParser::Init(dstParser, args.positional.data.data[4].data, args.positional.data.data[4].size);

    int64_t srcLineVal = StringParser::ParseLong(srcParser);
    int64_t dstLineVal = StringParser::ParseLong(dstParser);

    if (srcLineVal < 1 || dstLineVal < 1)
    {
        s_exitCode = 1;
        s_errorTag = "bad_args";
        LOG("ERROR: line numbers must be >= 1");
        return;
    }

    bool copyIncludes = false;
    bool showReturns = false;
    for (uint64_t i = 5; i < args.positional.size; ++i)
    {
        if (ByteviewEq(args.positional.data.data[i], "--copy-includes"))
            copyIncludes = true;
        else if (ByteviewEq(args.positional.data.data[i], "--show-returns"))
            showReturns = true;
    }

    CopyResult result =
        CopyBlock(MakeCStringPath(srcFilePath, alloc), (uint32_t)(srcLineVal - 1), MakeCStringPath(dstFilePath, alloc),
                  (uint32_t)(dstLineVal - 1), showReturns, copyIncludes, alloc);
    if (!result.ok)
    {
        s_exitCode = 1;
        s_errorTag = "copy_failed";
    }
    else
    {
        ShowFileBlocks(dstFilePath, alloc);
    }
}

void Run(region_alloc &alloc)
{
    cli_args args = ParseArgs(alloc);
    s_errorTag = nullptr;

    struct timespec t_start;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

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
        uint32_t pick = 0; // deterministic: could be seeded from PID or time
        LOG("%s", quotes[pick]);
        LOG("Usage: xcav <command> [args...]");
        LOG("Commands:");
        LOG("  blocks <file|dir>          List structural blocks");
        LOG("  read <file> <line>         Print block (un-indented, agent-friendly)");
        LOG("  move <file> <line> <to>    Move a block within file");
        LOG("  move-into <src> <ln> <dst> <ln> [--copy-includes]");
        LOG("  delete <file> <line>       Delete a block");
        LOG("  edit <file> <old> <new>    Line-based replacement (ignores indent)");
        LOG("  replace <file> <ln> <o> <n> Scoped replace within a block");
        LOG("  replace-block <file> <ln> [<new>] Replace entire block (stdin if no file)");
        LOG("  copy <src> <ln> <dst> <ln> Copy block between files");
        LOG("  insert --before|before|--after|after <file> <ln> <content> Insert code at a block boundary");
        LOG("  undo <file>                Restore from backup");
        LOG("  help                      Print detailed help");
        LOG("  onboard                   Print agent onboarding guide");
        LogUsage(&alloc, args, t_start, s_exitCode, s_errorTag);
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
    else if (ByteviewEq(args.command, "copy"))
        CmdCopy(alloc, args);
    else if (ByteviewEq(args.command, "insert"))
        CmdInsert(alloc, args);
    else if (ByteviewEq(args.command, "help"))
        CmdHelp();
    else if (ByteviewEq(args.command, "onboard"))
        CmdOnboard();

    LogUsage(&alloc, args, t_start, s_exitCode, s_errorTag);
}

} // namespace

void UserMain()
{
    region_alloc alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    Run(alloc);

    if (s_exitCode != 0)
        Exit(s_exitCode);
}

} // namespace nyla
