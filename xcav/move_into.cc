#include "xcav/move_into.h"

#include "xcav/backup.h"
#include "xcav/block_query.h"
#include "xcav/language.h"
#include "xcav/text_util.h"
#include "xcav/tree_sitter_util.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

auto MoveBlockInto(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, uint32_t dstLine, bool copyIncludes,
                   region_alloc &alloc) -> bool
{
    // ── Locate source block ──
    block_loc bl = LocateBlock(srcFilePath, srcLine, alloc);
    if (!bl.tree)
        return false;

    byteview srcSource = bl.source;

    // Extract source block text, extended to full line boundaries
    // (tree-sitter ranges may start after the line indent)
    uint32_t srcStart = LineStartOffset(srcSource, bl.range.startRow);
    uint32_t srcBlockLen = bl.range.endByte - srcStart;
    byteview srcBlockText{srcSource.data + srcStart, srcBlockLen};

    // Compute original indent of the block in source (whitespace before first token)
    uint32_t srcBaseIndent = bl.range.startByte - srcStart;
    for (uint32_t i = 0; i < srcBaseIndent; ++i)
    {
        if (srcSource.data[srcStart + i] != ' ' && srcSource.data[srcStart + i] != '\t')
        {
            srcBaseIndent = i;
            break;
        }
    }
    // ── Read destination file ──
    file_handle dstFh = FileOpen(dstFilePath, FileOpenMode::Read);
    if (!FileValid(dstFh))
    {
        LOG("ERROR: cannot open destination '%.*s'", (int)dstFilePath.size, dstFilePath.data);
        TreeDelete(bl.tree);
        return false;
    }
    byteview dstSource = FileReadFully(alloc, dstFh);
    FileClose(dstFh);

    // Determine destination insertion offset (start of line after dstLine)
    uint32_t dstOffset = LineStartOffset(dstSource, dstLine + 1);

    // Compute target indentation
    uint32_t dstLineStart = LineStartOffset(dstSource, dstLine);
    uint32_t dstLineEnd = LineEndOffset(dstSource, dstLine);

    bool dstLineBlank = true;
    for (uint32_t i = dstLineStart; i < dstLineEnd; ++i)
    {
        if (dstSource.data[i] != ' ' && dstSource.data[i] != '\t')
        {
            dstLineBlank = false;
            break;
        }
    }

    uint32_t targetIndent = LineIndent(dstSource, dstLineStart);
    if (dstLineBlank && dstLine > 0)
    {
        uint32_t prevLine = LineStartOffset(dstSource, dstLine - 1);
        targetIndent = LineIndent(dstSource, prevLine);
    }

    // ── Build re-indented block text for destination ──
    inline_vec<uint8_t, 65536> reindented{};
    {
        uint32_t pos = 0;
        while (pos < srcBlockText.size)
        {
            uint32_t lineIndent = 0;
            while (pos + lineIndent < srcBlockText.size &&
                   (srcBlockText.data[pos + lineIndent] == ' ' || srcBlockText.data[pos + lineIndent] == '\t'))
                ++lineIndent;

            int32_t relativeIndent = (int32_t)lineIndent - (int32_t)srcBaseIndent;
            if (relativeIndent < 0)
                relativeIndent = 0;
            uint32_t newIndent = targetIndent + (uint32_t)relativeIndent;

            for (uint32_t j = 0; j < newIndent; ++j)
                InlineVec::Append(reindented, (uint8_t)' ');

            pos += lineIndent;

            // Strip 'static ' from the first line of the moved block --
            // the function is no longer file-local after a cross-file move.
            if (pos == lineIndent && pos + 7 <= srcBlockText.size && srcBlockText.data[pos] == 's' &&
                srcBlockText.data[pos + 1] == 't' && srcBlockText.data[pos + 2] == 'a' &&
                srcBlockText.data[pos + 3] == 't' && srcBlockText.data[pos + 4] == 'i' &&
                srcBlockText.data[pos + 5] == 'c' && srcBlockText.data[pos + 6] == ' ')
                pos += 7;

            while (pos < srcBlockText.size && srcBlockText.data[pos] != '\n')
            {
                InlineVec::Append(reindented, srcBlockText.data[pos]);
                ++pos;
            }
            if (pos < srcBlockText.size && srcBlockText.data[pos] == '\n')
            {
                InlineVec::Append(reindented, (uint8_t)'\n');
                ++pos;
            }
        }
    }

    // ── Copy includes/imports if requested ──
    inline_vec<uint8_t, 16384> includeBlock{};
    if (copyIncludes)
    {
        uint32_t pos = 0;
        source_language lang = DetectLanguage(dstFilePath);
        while (pos < srcSource.size)
        {
            uint32_t lineStart = pos;
            while (lineStart < srcSource.size &&
                   (srcSource.data[lineStart] == ' ' || srcSource.data[lineStart] == '\t'))
                ++lineStart;

            uint32_t lineEnd = lineStart;
            while (lineEnd < srcSource.size && srcSource.data[lineEnd] != '\n')
                ++lineEnd;

            bool isInclude = false;
            if (lang == source_language::C || lang == source_language::Cpp)
            {
                if (lineStart + 1 < lineEnd && srcSource.data[lineStart] == '#')
                {
                    uint32_t p = lineStart + 1;
                    while (p < lineEnd && (srcSource.data[p] == ' ' || srcSource.data[p] == '\t'))
                        ++p;
                    if (p + 7 <= lineEnd && MemEq(srcSource.data + p, "include", 7))
                        isInclude = true;
                }
            }
            else if (lang == source_language::Java)
            {
                if (lineStart + 6 < lineEnd && MemEq(srcSource.data + lineStart, "import", 6))
                    isInclude = true;
            }
            else if (lang == source_language::JavaScript || lang == source_language::TypeScript ||
                     lang == source_language::Tsx)
            {
                if (lineStart + 6 < lineEnd && MemEq(srcSource.data + lineStart, "import", 6))
                    isInclude = true;
            }

            if (isInclude)
            {
                uint32_t includeLen = lineEnd - lineStart;
                byteview includeLine{srcSource.data + lineStart, includeLen};

                bool alreadyExists = false;
                {
                    uint32_t dp = 0;
                    while (dp < dstSource.size)
                    {
                        uint32_t dlStart = dp;
                        while (dlStart < dstSource.size &&
                               (dstSource.data[dlStart] == ' ' || dstSource.data[dlStart] == '\t'))
                            ++dlStart;
                        uint32_t dlEnd = dlStart;
                        while (dlEnd < dstSource.size && dstSource.data[dlEnd] != '\n')
                            ++dlEnd;
                        uint32_t dlLen = dlEnd - dlStart;
                        if (dlLen == includeLen && MemEq(dstSource.data + dlStart, includeLine.data, includeLen))
                        {
                            alreadyExists = true;
                            break;
                        }
                        dp = dlEnd + 1;
                        if (dp > dstSource.size)
                            break;
                    }
                }

                if (!alreadyExists)
                {
                    for (uint32_t i = 0; i < includeLen; ++i)
                        InlineVec::Append(includeBlock, includeLine.data[i]);
                    InlineVec::Append(includeBlock, (uint8_t)'\n');
                }
            }

            pos = lineEnd + 1;
            if (pos > srcSource.size)
                break;
        }

        if (includeBlock.size > 0)
            LOG("INFO: copying include/import lines");
    }

    // ── Find where existing includes/imports end in destination ──
    uint32_t dstIncludeEnd = 0;
    {
        source_language dstLang = DetectLanguage(dstFilePath);
        uint32_t pos = 0;
        while (pos < dstSource.size)
        {
            uint32_t ls = pos;
            while (ls < dstSource.size && (dstSource.data[ls] == ' ' || dstSource.data[ls] == '\t'))
                ++ls;
            uint32_t le = ls;
            while (le < dstSource.size && dstSource.data[le] != '\n')
                ++le;

            bool isInc = false;
            if (dstLang == source_language::C || dstLang == source_language::Cpp)
            {
                if (ls + 1 < le && dstSource.data[ls] == '#')
                {
                    uint32_t p = ls + 1;
                    while (p < le && (dstSource.data[p] == ' ' || dstSource.data[p] == '\t'))
                        ++p;
                    if (p + 7 <= le && MemEq(dstSource.data + p, "include", 7))
                        isInc = true;
                }
            }
            else if (dstLang == source_language::Java)
            {
                if (ls + 6 < le && MemEq(dstSource.data + ls, "import", 6))
                    isInc = true;
            }
            else if (dstLang == source_language::JavaScript || dstLang == source_language::TypeScript ||
                     dstLang == source_language::Tsx)
            {
                if (ls + 6 < le && MemEq(dstSource.data + ls, "import", 6))
                    isInc = true;
            }

            if (isInc)
                dstIncludeEnd = le + 1;

            pos = le + 1;
            if (pos > dstSource.size)
                break;
        }
    }

    // ── Build destination output ──
    uint64_t dstExtraSize = reindented.size + includeBlock.size;
    uint64_t dstTotalSize = dstSource.size + dstExtraSize;
    span<uint8_t> dstOutput = RegionAlloc::AllocArray<uint8_t>(alloc, dstTotalSize);

    uint64_t dstOutPos = 0;
    MemCpy(dstOutput.data + dstOutPos, dstSource.data, dstIncludeEnd);
    dstOutPos += dstIncludeEnd;
    if (includeBlock.size > 0)
    {
        MemCpy(dstOutput.data + dstOutPos, includeBlock.data.data, includeBlock.size);
        dstOutPos += includeBlock.size;
    }
    MemCpy(dstOutput.data + dstOutPos, dstSource.data + dstIncludeEnd, dstOffset - dstIncludeEnd);
    dstOutPos += dstOffset - dstIncludeEnd;
    MemCpy(dstOutput.data + dstOutPos, reindented.data.data, reindented.size);
    dstOutPos += reindented.size;
    MemCpy(dstOutput.data + dstOutPos, dstSource.data + dstOffset, dstSource.size - dstOffset);
    dstOutPos += dstSource.size - dstOffset;
    ASSERT(dstOutPos == dstTotalSize);

    // ── Build source output (block removed, cleaned up) ──
    // Extend to full line boundaries: tree-sitter ranges may start mid-line
    uint32_t cutStart = LineStartOffset(srcSource, bl.range.startRow);
    uint32_t cutEnd = bl.range.endByte;
    // Eat leading whitespace+newlines before the block
    while (cutStart > 0 && (srcSource.data[cutStart - 1] == ' ' || srcSource.data[cutStart - 1] == '\t' ||
                            srcSource.data[cutStart - 1] == '\n'))
    {
        --cutStart;
        if (srcSource.data[cutStart] == '\n')
            break;
    }

    // Eat preceding // comment lines (block-level doc comments only)
    {
        uint32_t commentCut = cutStart;
        while (commentCut > 0)
        {
            uint32_t lineStart = commentCut;
            while (lineStart > 0 && srcSource.data[lineStart - 1] != '\n')
                --lineStart;
            uint32_t contentStart = lineStart;
            while (contentStart < commentCut &&
                   (srcSource.data[contentStart] == ' ' || srcSource.data[contentStart] == '\t'))
                ++contentStart;
            bool isComment = (contentStart + 1 < commentCut && srcSource.data[contentStart] == '/' &&
                              srcSource.data[contentStart + 1] == '/');
            if (!isComment)
                break;
            commentCut = lineStart;
            while (commentCut > 0 && (srcSource.data[commentCut - 1] == ' ' || srcSource.data[commentCut - 1] == '\t' ||
                                      srcSource.data[commentCut - 1] == '\n'))
            {
                --commentCut;
                if (srcSource.data[commentCut] == '\n')
                    break;
            }
        }
        if (commentCut > 0)
        {
            bool hasContent = false;
            for (uint32_t i = 0; i < commentCut; ++i)
            {
                if (srcSource.data[i] != ' ' && srcSource.data[i] != '\t' && srcSource.data[i] != '\n')
                {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent)
                cutStart = commentCut;
        }
    }

    if (cutEnd < srcSource.size && srcSource.data[cutEnd] == '\n')
        ++cutEnd;

    uint64_t srcTotalSize = cutStart + (srcSource.size - cutEnd);
    span<uint8_t> srcOutput = RegionAlloc::AllocArray<uint8_t>(alloc, srcTotalSize);
    MemCpy(srcOutput.data, srcSource.data, cutStart);
    MemCpy(srcOutput.data + cutStart, srcSource.data + cutEnd, srcSource.size - cutEnd);

    TreeDelete(bl.tree);

    // ── Save backups and write both files ──
    SaveBackup(srcFilePath, alloc);
    SaveBackup(dstFilePath, alloc);

    // Write destination
    file_handle wFh = FileOpen(dstFilePath, FileOpenMode::Write);
    if (!FileValid(wFh))
    {
        LOG("ERROR: cannot write destination '%.*s'", (int)dstFilePath.size, dstFilePath.data);
        return false;
    }
    uint32_t dstWritten = FileWrite(wFh, (uint32_t)dstTotalSize, dstOutput.data);
    FileClose(wFh);

    if (dstWritten != dstTotalSize)
    {
        LOG("ERROR: short write on destination — file may be corrupt");
        return false;
    }

    // Write source
    wFh = FileOpen(srcFilePath, FileOpenMode::Write);
    if (!FileValid(wFh))
    {
        LOG("ERROR: cannot write source '%.*s'", (int)srcFilePath.size, srcFilePath.data);
        return false;
    }
    uint32_t srcWritten = FileWrite(wFh, (uint32_t)srcTotalSize, srcOutput.data);
    FileClose(wFh);

    if (srcWritten != srcTotalSize)
    {
        LOG("ERROR: short write on source — file may be corrupt");
        return false;
    }

    LOG("OK: moved %s block (lines %u-%u) cross-file", bl.type, bl.range.startRow + 1, bl.range.endRow + 1);
    return true;
}

} // namespace nyla
