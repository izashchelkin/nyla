#include "xcav/edit_ops.h"
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

struct normalized_edit_text
{
    byteview text;
    span<uint32_t> normalizedToOriginal;
};

struct edit_unicode_replacement
{
    uint8_t first;
    uint8_t second;
    uint32_t skip;
};

static auto GetEditUnicodeReplacement(byteview text, uint32_t i, edit_unicode_replacement &out) -> bool
{
    if (i + 2 >= text.size || text.data[i] != 0xE2)
        return false;

    uint8_t c1 = text.data[i + 1];
    uint8_t c2 = text.data[i + 2];
    out = {};

    if (c1 == 0x80 && c2 == 0x94)
        out = edit_unicode_replacement{'-', '-', 2};
    else if (c1 == 0x80 && c2 == 0x93)
        out = edit_unicode_replacement{'-', 0, 2};
    else if (c1 == 0x86 && c2 == 0x92)
        out = edit_unicode_replacement{'-', '>', 2};
    else if (c1 == 0x86 && c2 == 0x90)
        out = edit_unicode_replacement{'<', '-', 2};
    else if (c1 == 0x80 && c2 == 0x9C)
        out = edit_unicode_replacement{'"', 0, 2};
    else if (c1 == 0x80 && c2 == 0x9D)
        out = edit_unicode_replacement{'"', 0, 2};
    else if (c1 == 0x80 && c2 == 0x98)
        out = edit_unicode_replacement{'\'', 0, 2};
    else if (c1 == 0x80 && c2 == 0x99)
        out = edit_unicode_replacement{'\'', 0, 2};
    else
        return false;

    return true;
}

static auto NormalizeForEdit(byteview text, region_alloc &alloc, bool keepPositionMap) -> normalized_edit_text
{
    if (text.size == 0)
        return normalized_edit_text{text, span<uint32_t>{}};

    bool needsNormalization = false;
    for (uint32_t i = 0; i < text.size; ++i)
    {
        edit_unicode_replacement replacement{};
        if (GetEditUnicodeReplacement(text, i, replacement))
        {
            needsNormalization = true;
            break;
        }
    }

    if (!needsNormalization)
        return normalized_edit_text{text, span<uint32_t>{}};

    span<uint8_t> out = RegionAlloc::AllocArray<uint8_t>(alloc, text.size);
    span<uint32_t> posMap{};
    if (keepPositionMap)
        posMap = RegionAlloc::AllocArray<uint32_t>(alloc, text.size);

    uint32_t outSize = 0;

    auto appendByte = [&](uint8_t c, uint32_t originalPos) -> void {
        ASSERT(outSize < text.size);
        out.data[outSize] = c;
        if (keepPositionMap)
            posMap.data[outSize] = originalPos;
        ++outSize;
    };

    for (uint32_t i = 0; i < text.size; ++i)
    {
        edit_unicode_replacement replacement{};
        if (GetEditUnicodeReplacement(text, i, replacement))
        {
            appendByte(replacement.first, i);
            if (replacement.second != 0)
                appendByte(replacement.second, i + replacement.skip);
            i += replacement.skip;
            continue;
        }

        appendByte(text.data[i], i);
    }

    return normalized_edit_text{byteview{out.data, outSize},
                                keepPositionMap ? span<uint32_t>{posMap.data, outSize} : span<uint32_t>{}};
}

// ─── MoveBlock ──────────────────────────────────────────────────────────────

auto MoveBlock(byteview filePath, uint32_t blockLine, uint32_t destLine, region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    byteview source = bl.source;


    // Extract block text
    uint32_t blockLen = bl.range.endByte - bl.range.startByte;
    byteview blockText{source.data + bl.range.startByte, blockLen};

    // Determine destination byte offset (start of line after destLine)
    uint32_t destOffset = LineStartOffset(source, destLine + 1);

    // Compute destination indentation
    uint32_t destLineStart = LineStartOffset(source, destLine);
    uint32_t destLineEnd = LineEndOffset(source, destLine);

    bool destLineBlank = true;
    for (uint32_t i = destLineStart; i < destLineEnd; ++i)
    {
        if (source.data[i] != ' ' && source.data[i] != '\t')
        {
            destLineBlank = false;
            break;
        }
    }

    uint32_t targetIndent = LineIndent(source, destLineStart);
    if (destLineBlank && destLine > 0)
    {
        uint32_t prevLine = LineStartOffset(source, destLine - 1);
        targetIndent = LineIndent(source, prevLine);
    }

    // Original indent of the block in source
    uint32_t originalIndent = 0;
    {
        uint32_t lineStart = LineStartOffset(source, bl.range.startRow);
        while (lineStart + originalIndent < bl.range.startByte &&
               (source.data[lineStart + originalIndent] == ' ' || source.data[lineStart + originalIndent] == '\t'))
            ++originalIndent;
    }

    // Re-indented block text
    inline_vec<uint8_t, 65536> reindented{};

    uint32_t pos = 0;
    while (pos < blockText.size)
    {
        uint32_t lineIndent = 0;
        while (pos + lineIndent < blockText.size &&
               (blockText.data[pos + lineIndent] == ' ' || blockText.data[pos + lineIndent] == '\t'))
            ++lineIndent;

        int32_t relativeIndent = (int32_t)lineIndent - (int32_t)originalIndent;
        if (relativeIndent < 0)
            relativeIndent = 0;

        uint32_t newIndent = targetIndent + (uint32_t)relativeIndent;

        for (uint32_t j = 0; j < newIndent; ++j)
            InlineVec::Append(reindented, (uint8_t)' ');

        pos += lineIndent;
        while (pos < blockText.size && blockText.data[pos] != '\n')
        {
            InlineVec::Append(reindented, blockText.data[pos]);
            ++pos;
        }
        if (pos < blockText.size && blockText.data[pos] == '\n')
        {
            InlineVec::Append(reindented, (uint8_t)'\n');
            ++pos;
        }
    }

    // Assemble output. Case A: block before dest. Case B: block after dest.
    uint64_t totalSize;
    span<uint8_t> output;

    if (bl.range.endByte <= destOffset)
    {
        totalSize = bl.range.startByte + (destOffset - bl.range.endByte) + reindented.size +
                    (source.size - destOffset);
        output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);

        uint64_t outPos = 0;
        MemCpy(output.data + outPos, source.data, bl.range.startByte);
        outPos += bl.range.startByte;
        MemCpy(output.data + outPos, source.data + bl.range.endByte, destOffset - bl.range.endByte);
        outPos += destOffset - bl.range.endByte;
        MemCpy(output.data + outPos, reindented.data.data, reindented.size);
        outPos += reindented.size;
        MemCpy(output.data + outPos, source.data + destOffset, source.size - destOffset);
        outPos += source.size - destOffset;
        ASSERT(outPos == totalSize);
    }
    else if (bl.range.startByte >= destOffset)
    {
        totalSize = destOffset + reindented.size + (bl.range.startByte - destOffset) +
                    (source.size - bl.range.endByte);
        output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);

        uint64_t outPos = 0;
        MemCpy(output.data + outPos, source.data, destOffset);
        outPos += destOffset;
        MemCpy(output.data + outPos, reindented.data.data, reindented.size);
        outPos += reindented.size;
        MemCpy(output.data + outPos, source.data + destOffset, bl.range.startByte - destOffset);
        outPos += bl.range.startByte - destOffset;
        MemCpy(output.data + outPos, source.data + bl.range.endByte, source.size - bl.range.endByte);
        outPos += source.size - bl.range.endByte;
        ASSERT(outPos == totalSize);
    }
    else
    {
        LOG("ERROR: block to move contains the destination line — use xcav blocks to find block boundaries");
        TreeDelete(bl.tree);
        return false;
    }

    TreeDelete(bl.tree);

    SaveBackup(filePath, alloc);

    file_handle fh = FileOpen(filePath, FileOpenMode::Write);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    uint32_t written = FileWrite(fh, (uint32_t)totalSize, output.data);
    FileClose(fh);

    if (written != totalSize)
    {
        LOG("ERROR: short write — file may be corrupt");
        return false;
    }

    LOG("OK: moved %s block (lines %u-%u) to after line %u", bl.type, bl.range.startRow + 1, bl.range.endRow + 1, destLine + 1);
    return true;
}

// ─── MoveBlockInto ──────────────────────────────────────────────────────────

auto MoveBlockInto(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, uint32_t dstLine, bool copyIncludes,
                   region_alloc &alloc) -> bool
{
    // ── Locate source block ──
    block_loc bl = LocateBlock(srcFilePath, srcLine, alloc);
    if (!bl.tree)
        return false;

    byteview srcSource = bl.source;

    // Extract source block text
    uint32_t srcBlockLen = bl.range.endByte - bl.range.startByte;
    byteview srcBlockText{srcSource.data + bl.range.startByte, srcBlockLen};

    // Compute original indent of the block in source
    uint32_t srcBaseIndent = 0;
    {
        uint32_t lineStart = LineStartOffset(srcSource, bl.range.startRow);
        while (lineStart + srcBaseIndent < bl.range.startByte &&
               (srcSource.data[lineStart + srcBaseIndent] == ' ' || srcSource.data[lineStart + srcBaseIndent] == '\t'))
            ++srcBaseIndent;
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
    uint32_t cutStart = bl.range.startByte;
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
            while (commentCut > 0 &&
                   (srcSource.data[commentCut - 1] == ' ' || srcSource.data[commentCut - 1] == '\t' ||
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

// ─── DeleteBlock ────────────────────────────────────────────────────────────

auto DeleteBlock(byteview filePath, uint32_t blockLine, region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    uint32_t blockLen = bl.range.endByte - bl.range.startByte;


    TreeDelete(bl.tree);

    // Remove block + cleanup surrounding blank lines and leading comments.
    byteview source = bl.source;
    uint32_t cutStart = bl.range.startByte;
    uint32_t cutEnd = bl.range.endByte;

    // Eat leading whitespace+newlines before the block
    while (cutStart > 0 &&
           (source.data[cutStart - 1] == ' ' || source.data[cutStart - 1] == '\t' || source.data[cutStart - 1] == '\n'))
    {
        --cutStart;
        if (source.data[cutStart] == '\n')
            break; // eat exactly one leading blank line
    }

    // Eat preceding // comment lines (and their leading whitespace/newlines).
    {
        uint32_t commentCut = cutStart;
        while (commentCut > 0)
        {
            uint32_t lineStart = commentCut;
            while (lineStart > 0 && source.data[lineStart - 1] != '\n')
                --lineStart;

            uint32_t contentStart = lineStart;
            while (contentStart < commentCut &&
                   (source.data[contentStart] == ' ' || source.data[contentStart] == '\t'))
                ++contentStart;

            bool isComment = (contentStart + 1 < commentCut && source.data[contentStart] == '/' &&
                              source.data[contentStart + 1] == '/');
            if (!isComment)
                break;

            commentCut = lineStart;
            while (commentCut > 0 &&
                   (source.data[commentCut - 1] == ' ' || source.data[commentCut - 1] == '\t' ||
                    source.data[commentCut - 1] == '\n'))
            {
                --commentCut;
                if (source.data[commentCut] == '\n')
                    break;
            }
        }

        if (commentCut > 0)
        {
            bool hasContent = false;
            for (uint32_t i = 0; i < commentCut; ++i)
            {
                if (source.data[i] != ' ' && source.data[i] != '\t' && source.data[i] != '\n')
                {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent)
                cutStart = commentCut;
        }
    }

    // Eat one trailing newline after the block (if present)
    if (cutEnd < source.size && source.data[cutEnd] == '\n')
        ++cutEnd;

    // Build output: before cut + after cut
    uint64_t totalSize = cutStart + (source.size - cutEnd);
    span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    MemCpy(output.data, source.data, cutStart);
    MemCpy(output.data + cutStart, source.data + cutEnd, source.size - cutEnd);

    SaveBackup(filePath, alloc);

    file_handle fh = FileOpen(filePath, FileOpenMode::Write);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    uint32_t written = FileWrite(fh, (uint32_t)totalSize, output.data);
    FileClose(fh);

    if (written != totalSize)
    {
        LOG("ERROR: short write — file may be corrupt");
        return false;
    }

    LOG("OK: deleted %s block (lines %u-%u)", bl.type, bl.range.startRow + 1, bl.range.endRow + 1);
    return true;
}

// ─── ReplaceBlock ───────────────────────────────────────────────────────────

auto ReplaceBlock(byteview filePath, uint32_t blockLine, byteview newFilePath, region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    LOG("INFO: replacing %s block (lines %u-%u)", bl.type, bl.range.startRow + 1, bl.range.endRow + 1);

    // Read new content from temp file
    file_handle newFh = FileOpen(newFilePath, FileOpenMode::Read);
    if (!FileValid(newFh))
    {
        TreeDelete(bl.tree);
        LOG("ERROR: cannot read new-text file '%.*s'", (int)newFilePath.size, newFilePath.data);
        return false;
    }
    byteview newText = FileReadFully(alloc, newFh);
    FileClose(newFh);

    // Determine base indent from the old block
    byteview source = bl.source;
    uint32_t baseIndent = 0;
    {
        uint32_t lineStart = LineStartOffset(source, bl.range.startRow);
        while (lineStart + baseIndent < bl.range.startByte &&
               (source.data[lineStart + baseIndent] == ' ' || source.data[lineStart + baseIndent] == '\t'))
            ++baseIndent;
    }

    // Find minimum indentation in newText (first non-blank line's indent)
    uint32_t newMinIndent = 0;
    bool foundNonBlank = false;
    {
        uint32_t p = 0;
        while (p < newText.size)
        {
            uint32_t lineIndent = 0;
            while (p + lineIndent < newText.size &&
                   (newText.data[p + lineIndent] == ' ' || newText.data[p + lineIndent] == '\t'))
                ++lineIndent;
            uint32_t contentStart = p + lineIndent;
            uint32_t lineEnd = contentStart;
            while (lineEnd < newText.size && newText.data[lineEnd] != '\n')
                ++lineEnd;
            // Non-blank line? (has content beyond indent)
            if (lineEnd > contentStart)
            {
                newMinIndent = lineIndent;
                foundNonBlank = true;
                break;
            }
            p = lineEnd;
            if (p < newText.size && newText.data[p] == '\n')
                ++p;
        }
    }

    // Re-indent newText to baseIndent
    inline_vec<uint8_t, 65536> indentedNew{};
    {
        uint32_t p = 0;
        while (p < newText.size)
        {
            uint32_t lineIndent = 0;
            while (p + lineIndent < newText.size &&
                   (newText.data[p + lineIndent] == ' ' || newText.data[p + lineIndent] == '\t'))
                ++lineIndent;

            int32_t relativeIndent = (int32_t)lineIndent - (int32_t)newMinIndent;
            if (relativeIndent < 0)
                relativeIndent = 0;
            uint32_t newIndent = baseIndent + (uint32_t)relativeIndent;

            for (uint32_t j = 0; j < newIndent; ++j)
                InlineVec::Append(indentedNew, (uint8_t)' ');

            p += lineIndent;
            while (p < newText.size && newText.data[p] != '\n')
            {
                InlineVec::Append(indentedNew, newText.data[p]);
                ++p;
            }
            if (p < newText.size && newText.data[p] == '\n')
            {
                InlineVec::Append(indentedNew, (uint8_t)'\n');
                ++p;
            }
        }
    }

    // Ensure indentedNew ends with newline
    if (indentedNew.size > 0 && indentedNew.data.data[indentedNew.size - 1] != '\n')
        InlineVec::Append(indentedNew, (uint8_t)'\n');

    TreeDelete(bl.tree);

    // Compute cutStart/cutEnd with same cleanup as DeleteBlock
    uint32_t cutStart = bl.range.startByte;
    uint32_t cutEnd = bl.range.endByte;

    // Eat leading whitespace+newlines before the block
    while (cutStart > 0 &&
           (source.data[cutStart - 1] == ' ' || source.data[cutStart - 1] == '\t' ||
            source.data[cutStart - 1] == '\n'))
    {
        --cutStart;
        if (source.data[cutStart] == '\n')
            break;
    }

    // Eat preceding // comment lines
    {
        uint32_t commentCut = cutStart;
        while (commentCut > 0)
        {
            uint32_t lineStart = commentCut;
            while (lineStart > 0 && source.data[lineStart - 1] != '\n')
                --lineStart;
            uint32_t contentStart = lineStart;
            while (contentStart < commentCut &&
                   (source.data[contentStart] == ' ' || source.data[contentStart] == '\t'))
                ++contentStart;
            bool isComment = (contentStart + 1 < commentCut && source.data[contentStart] == '/' &&
                              source.data[contentStart + 1] == '/');
            if (!isComment)
                break;
            commentCut = lineStart;
            while (commentCut > 0 &&
                   (source.data[commentCut - 1] == ' ' || source.data[commentCut - 1] == '\t' ||
                    source.data[commentCut - 1] == '\n'))
            {
                --commentCut;
                if (source.data[commentCut] == '\n')
                    break;
            }
        }
        if (commentCut > 0)
        {
            bool hasContent = false;
            for (uint32_t i = 0; i < commentCut; ++i)
            {
                if (source.data[i] != ' ' && source.data[i] != '\t' && source.data[i] != '\n')
                {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent)
                cutStart = commentCut;
        }
    }

    // Eat one trailing newline
    if (cutEnd < source.size && source.data[cutEnd] == '\n')
        ++cutEnd;

    // Build output: before + new content + newline + after
    uint64_t totalSize = cutStart + indentedNew.size + (source.size - cutEnd);
    span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    MemCpy(output.data, source.data, cutStart);
    MemCpy(output.data + cutStart, indentedNew.data.data, indentedNew.size);
    MemCpy(output.data + cutStart + indentedNew.size, source.data + cutEnd, source.size - cutEnd);

    SaveBackup(filePath, alloc);

    file_handle fh = FileOpen(filePath, FileOpenMode::Write);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    uint32_t written = FileWrite(fh, (uint32_t)totalSize, output.data);
    FileClose(fh);

    if (written != totalSize)
    {
        LOG("ERROR: short write — file may be corrupt");
        return false;
    }

    LOG("OK: replaced %s block (was lines %u-%u)", bl.type, bl.range.startRow + 1, bl.range.endRow + 1);
    return true;
}

// ─── ReplaceInBlock ─────────────────────────────────────────────────────────

auto ReplaceInBlock(byteview filePath, uint32_t blockLine, byteview oldText, byteview newText, region_alloc &alloc)
    -> bool
{
    // Normalize Unicode
    {
        inline_vec<uint8_t, 65536> normOld{}, normNew{};
        for (uint32_t i = 0; i < oldText.size; ++i)
            InlineVec::Append(normOld, oldText.data[i]);
        for (uint32_t i = 0; i < newText.size; ++i)
            InlineVec::Append(normNew, newText.data[i]);
        NormalizeText(normOld);
        NormalizeText(normNew);
        if (normOld.size != oldText.size)
        {
            span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, normOld.size);
            MemCpy(buf.data, normOld.data.data, normOld.size);
            oldText = byteview{buf.data, normOld.size};
        }
        if (normNew.size != newText.size)
        {
            span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, normNew.size);
            MemCpy(buf.data, normNew.data.data, normNew.size);
            newText = byteview{buf.data, normNew.size};
        }
    }

    if (oldText.size == 0)
    {
        LOG("ERROR: oldText cannot be empty — provide text to replace");
        return false;
    }

    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    // Search for oldText within the block's byte range
    byteview source = bl.source;
    uint32_t matchPos = 0;
    uint32_t matchCount = 0;
    for (uint32_t i = bl.range.startByte; i + oldText.size <= bl.range.endByte; ++i)
    {
        if (MemEq(source.data + i, oldText.data, oldText.size))
        {
            matchPos = i;
            ++matchCount;
        }
    }

    TreeDelete(bl.tree);

    if (matchCount == 0)
    {
        LOG("ERROR: oldText not found within block at line %u — use xcav read --raw to check", blockLine + 1);
        return false;
    }

    if (matchCount > 1)
    {
        LOG("ERROR: oldText found %u times within block — still ambiguous, use more context", matchCount);
        return false;
    }

    // Build output: before + new + after
    uint64_t totalSize = source.size - oldText.size + newText.size;
    span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    MemCpy(output.data, source.data, matchPos);
    MemCpy(output.data + matchPos, newText.data, newText.size);
    MemCpy(output.data + matchPos + newText.size, source.data + matchPos + oldText.size,
           source.size - (matchPos + oldText.size));

    SaveBackup(filePath, alloc);

    file_handle fh = FileOpen(filePath, FileOpenMode::Write);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    uint32_t written = FileWrite(fh, (uint32_t)totalSize, output.data);
    FileClose(fh);

    if (written != totalSize)
    {
        LOG("ERROR: short write — file may be corrupt");
        return false;
    }

    LOG("OK: replaced in block at line %u", blockLine + 1);
    return true;
}

// ─── EditSafe ───────────────────────────────────────────────────────────────

auto EditSafe(byteview filePath, byteview oldFile, byteview newFile, region_alloc &alloc, bool force, bool dryRun,
              bool diff) -> bool
{
    // Read old and new text from temp files
    file_handle oldFh = FileOpen(oldFile, FileOpenMode::Read);
    if (!FileValid(oldFh))
    {
        LOG("ERROR: cannot open old-text file '%.*s'", (int)oldFile.size, oldFile.data);
        return false;
    }
    byteview oldText = FileReadFully(alloc, oldFh);
    FileClose(oldFh);

    file_handle newFh = FileOpen(newFile, FileOpenMode::Read);
    if (!FileValid(newFh))
    {
        LOG("ERROR: cannot open new-text file '%.*s'", (int)newFile.size, newFile.data);
        return false;
    }
    byteview newText = FileReadFully(alloc, newFh);
    FileClose(newFh);

    // Normalize Unicode: LLMs may emit em-dash, arrows, smart quotes etc.
    oldText = NormalizeForEdit(oldText, alloc, false).text;
    newText = NormalizeForEdit(newText, alloc, false).text;

    if (oldText.size == 0)
    {
        LOG("ERROR: oldText cannot be empty — provide text to replace");
        return false;
    }

    // Read the target file
    file_handle fh = FileOpen(filePath, FileOpenMode::Read);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot open '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    byteview source = FileReadFully(alloc, fh);
    FileClose(fh);

    // ── Normalize source for Unicode-safe matching ──
    // oldText is already normalized (em-dash→--, curly quotes→straight, etc.).
    // Source must be normalized too, otherwise Unicode chars in the file
    // cause "oldText not found" even when the text is byte-identical.
    normalized_edit_text normalizedSource = NormalizeForEdit(source, alloc, true);
    byteview normSource = normalizedSource.text;
    span<uint32_t> normToOrig = normalizedSource.normalizedToOriginal;

    // ─── Stage 0: find oldText in source ──────────────────────────────────
    uint32_t matchPos = 0;
    uint32_t matchEnd = 0;
    uint32_t matchCount = 0;
    bool isLaxMatch = false;

    // ─── Strategy A: line-based lax matching ─────────────────────────
    {
        struct oldLine
        {
            uint32_t contentStart;
            uint32_t contentEnd;
        };
        span<oldLine> oldLines = RegionAlloc::AllocArray<oldLine>(alloc, oldText.size);
        uint32_t oldLineCount = 0;
        {
            uint32_t p = 0;
            while (p < oldText.size)
            {
                while (p < oldText.size && (oldText.data[p] == ' ' || oldText.data[p] == '\t'))
                    ++p;
                uint32_t cStart = p;
                while (p < oldText.size && oldText.data[p] != '\n')
                    ++p;
                uint32_t cEnd = p;
                while (cEnd > cStart && (oldText.data[cEnd - 1] == ' ' || oldText.data[cEnd - 1] == '\t'))
                    --cEnd;
                oldLine ol = {cStart, cEnd};
                ASSERT(oldLineCount < oldLines.size);
                oldLines.data[oldLineCount++] = ol;
                if (p < oldText.size && oldText.data[p] == '\n')
                    ++p;
            }
        }

        if (oldLineCount > 0)
        {
            for (uint32_t pos = 0; pos < normSource.size;)
            {
                uint32_t scanPos = pos;
                bool allMatch = true;
                for (uint32_t li = 0; li < oldLineCount; ++li)
                {
                    if (scanPos >= normSource.size)
                    {
                        allMatch = false;
                        break;
                    }
                    while (scanPos < normSource.size && (normSource.data[scanPos] == ' ' || normSource.data[scanPos] == '\t'))
                        ++scanPos;
                    uint32_t srcContentStart = scanPos;
                    while (scanPos < normSource.size && normSource.data[scanPos] != '\n')
                        ++scanPos;
                    uint32_t srcContentEnd = scanPos;
                    while (srcContentEnd > srcContentStart &&
                           (normSource.data[srcContentEnd - 1] == ' ' || normSource.data[srcContentEnd - 1] == '\t'))
                        --srcContentEnd;

                    uint32_t oldLen = oldLines.data[li].contentEnd - oldLines.data[li].contentStart;
                    uint32_t srcLen = srcContentEnd - srcContentStart;
                    if (oldLen != srcLen || !MemEq(oldText.data + oldLines.data[li].contentStart,
                                                   normSource.data + srcContentStart, oldLen))
                    {
                        allMatch = false;
                        break;
                    }

                    if (scanPos < normSource.size && normSource.data[scanPos] == '\n')
                        ++scanPos;
                }
                if (allMatch)
                {
                    matchPos = pos;
                    matchEnd = scanPos;
                    ++matchCount;
                    isLaxMatch = true;
                }
                while (pos < normSource.size && normSource.data[pos] != '\n')
                    ++pos;
                if (pos < normSource.size && normSource.data[pos] == '\n')
                    ++pos;
            }
        }
    }

    // ─── Strategy B: byte-exact fallback ─────────────────────────────
    if (matchCount != 1)
    {
        matchCount = 0;
        isLaxMatch = false;
        for (uint32_t i = 0; i + oldText.size <= normSource.size; ++i)
        {
            if (MemEq(normSource.data + i, oldText.data, oldText.size))
            {
                matchPos = i;
                ++matchCount;
            }
        }
    }

    // ── Map match positions from normalized back to original source ──
    if (matchCount == 1 && normToOrig.data != nullptr)
    {
        matchPos = normToOrig.data[matchPos];
        // For lax matches, map matchEnd too
        if (isLaxMatch)
        {
            // matchEnd points to the byte after the last matched line's \n.
            // Map to original: find the corresponding \n in the original source
            if (matchEnd >= normSource.size)
                matchEnd = source.size;
            else if (matchEnd > 0 && normSource.data[matchEnd - 1] == '\n')
            {
                // Find the line end in original source starting from normToOrig[matchEnd-1]
                uint32_t origPos = normToOrig.data[matchEnd - 1];
                while (origPos < source.size && source.data[origPos] != '\n')
                    ++origPos;
                if (origPos < source.size)
                    ++origPos; // skip \n
                matchEnd = origPos;
            }
            else
            {
                matchEnd = matchPos + (uint32_t)oldText.size;
            }
        }
    }

    if (matchCount == 0)
    {
        LOG("ERROR: oldText not found in file — use xcav read --raw to get exact text");
        return false;
    }

    if (matchCount > 1)
    {
        LOG("ERROR: oldText found %u times — ambiguous, use more context or xcav read --raw", matchCount);
        return false;
    }

    LOG("INFO: found oldText at byte %u%s", matchPos, isLaxMatch ? " (lax match)" : "");

    // Require full-line edits: oldText must start at a line boundary and
    // end at a line boundary.
    uint32_t matchEndByte = isLaxMatch ? matchEnd : (matchPos + (uint32_t)oldText.size);
    bool startsAtLineStart = (matchPos == 0 || source.data[matchPos - 1] == '\n');
    bool endsAtLineEnd;
    if (isLaxMatch)
        endsAtLineEnd = (matchEnd >= source.size || source.data[matchEnd - 1] == '\n');
    else
        endsAtLineEnd = (matchEndByte >= source.size || source.data[matchEndByte] == '\n');
    if (!startsAtLineStart || !endsAtLineEnd)
    {
        LOG("ERROR: oldText must span full lines (starts at line start: %s, ends at line end: %s)",
            startsAtLineStart ? "yes" : "no", endsAtLineEnd ? "yes" : "no");
        LOG("INFO: use xcav read --raw to get exact full-line text to match");
        return false;
    }

    // For lax matches: replace oldText with the actual source bytes
    if (isLaxMatch)
    {
        if (oldText.size == 0 || oldText.data[oldText.size - 1] != '\n')
        {
            if (matchEnd > matchPos && source.data[matchEnd - 1] == '\n')
                --matchEnd;
        }
        uint32_t matchedLen = matchEnd - matchPos;
        span<uint8_t> srcOld = RegionAlloc::AllocArray<uint8_t>(alloc, matchedLen);
        MemCpy(srcOld.data, source.data + matchPos, matchedLen);
        oldText = byteview{srcOld.data, matchedLen};
    }

    // Determine base indent for the replacement region.
    uint32_t lineStart = 0;
    {
        uint32_t line = 0;
        for (uint32_t i = 0; i <= matchPos && i < source.size; ++i)
        {
            if (source.data[i] == '\n')
            {
                ++line;
                if (i + 1 <= matchPos)
                    lineStart = i + 1;
            }
        }
    }
    uint32_t baseIndent = 0;
    while (lineStart + baseIndent < source.size &&
           (source.data[lineStart + baseIndent] == ' ' || source.data[lineStart + baseIndent] == '\t'))
        ++baseIndent;

    // If oldText doesn't include the line's leading whitespace, expand matchPos
    {
        uint32_t expandedStart = matchPos;
        while (expandedStart > lineStart &&
               (source.data[expandedStart - 1] == ' ' || source.data[expandedStart - 1] == '\t'))
            --expandedStart;
        bool hasNewline = false;
        for (uint32_t i = expandedStart; i < matchPos; ++i)
            if (source.data[i] == '\n')
                hasNewline = true;
        if (!hasNewline && expandedStart < matchPos)
        {
            uint32_t indentLen = matchPos - expandedStart;
            inline_vec<uint8_t, 65536> expandedOld{};
            for (uint32_t i = expandedStart; i < matchPos; ++i)
                InlineVec::Append(expandedOld, source.data[i]);
            for (uint32_t i = 0; i < oldText.size; ++i)
                InlineVec::Append(expandedOld, oldText.data[i]);

            span<uint8_t> newOld = RegionAlloc::AllocArray<uint8_t>(alloc, expandedOld.size);
            MemCpy(newOld.data, expandedOld.data.data, expandedOld.size);
            oldText = byteview{newOld.data, expandedOld.size};
            matchPos = expandedStart;
            LOG("INFO: expanded oldText to include %u leading spaces", indentLen);
        }
    }

    // ─── Stage 1: apply replacement and re-indent newText ──────────────────

    uint32_t minIndent = 0xFFFFFFFF;
    {
        uint32_t p = 0;
        while (p < newText.size)
        {
            uint32_t lineIndent = 0;
            while (p + lineIndent < newText.size &&
                   (newText.data[p + lineIndent] == ' ' || newText.data[p + lineIndent] == '\t'))
                ++lineIndent;

            uint32_t contentStart = p + lineIndent;
            bool hasContent = false;
            while (contentStart < newText.size && newText.data[contentStart] != '\n' &&
                   newText.data[contentStart] != '\r')
            {
                if (newText.data[contentStart] != ' ' && newText.data[contentStart] != '\t')
                {
                    hasContent = true;
                    break;
                }
                ++contentStart;
            }

            if (hasContent && lineIndent < minIndent)
                minIndent = lineIndent;

            while (p < newText.size && newText.data[p] != '\n')
                ++p;
            if (p < newText.size)
                ++p;
        }
    }
    if (minIndent == 0xFFFFFFFF)
        minIndent = 0;

    // Build re-indented newText
    inline_vec<uint8_t, 65536> reindented{};
    {
        uint32_t pos = 0;
        while (pos < newText.size && (newText.data[pos] == '\n' || newText.data[pos] == '\r'))
        {
            InlineVec::Append(reindented, newText.data[pos]);
            ++pos;
        }

        while (pos < newText.size)
        {
            uint32_t lineIndent = 0;
            while (pos + lineIndent < newText.size &&
                   (newText.data[pos + lineIndent] == ' ' || newText.data[pos + lineIndent] == '\t'))
                ++lineIndent;

            int32_t relativeIndent = (int32_t)lineIndent - (int32_t)minIndent;
            if (relativeIndent < 0)
                relativeIndent = 0;
            uint32_t newIndent = baseIndent + (uint32_t)relativeIndent;

            for (uint32_t j = 0; j < newIndent; ++j)
                InlineVec::Append(reindented, (uint8_t)' ');

            pos += lineIndent;
            while (pos < newText.size && newText.data[pos] != '\n')
            {
                InlineVec::Append(reindented, newText.data[pos]);
                ++pos;
            }
            if (pos < newText.size && newText.data[pos] == '\n')
            {
                InlineVec::Append(reindented, (uint8_t)'\n');
                ++pos;
            }
        }
    }

    // ─── Stage 2: whitespace cleanup ────────────────────────────────────
    auto cleanupWhitespace = [&](inline_vec<uint8_t, 65536> &buf) -> void {
        inline_vec<uint8_t, 65536> cleaned{};
        uint32_t pos = 0;
        uint32_t blankRun = 0;

        while (pos < buf.size)
        {
            uint32_t lineEnd = pos;
            while (lineEnd < buf.size && buf.data[lineEnd] != '\n')
                ++lineEnd;

            bool isBlank = true;
            for (uint32_t i = pos; i < lineEnd; ++i)
            {
                uint8_t c = buf.data[i];
                if (c != ' ' && c != '\t')
                {
                    isBlank = false;
                    break;
                }
            }

            if (isBlank)
            {
                ++blankRun;
                if (blankRun <= 1)
                    InlineVec::Append(cleaned, (uint8_t)'\n');
            }
            else
            {
                blankRun = 0;

                uint32_t contentEnd = lineEnd;
                while (contentEnd > pos && (buf.data[contentEnd - 1] == ' ' || buf.data[contentEnd - 1] == '\t'))
                    --contentEnd;

                for (uint32_t i = pos; i < contentEnd; ++i)
                {
                    uint8_t c = buf.data[i];
                    if (c == '\t')
                    {
                        InlineVec::Append(cleaned, (uint8_t)' ');
                        InlineVec::Append(cleaned, (uint8_t)' ');
                        InlineVec::Append(cleaned, (uint8_t)' ');
                        InlineVec::Append(cleaned, (uint8_t)' ');
                    }
                    else
                    {
                        InlineVec::Append(cleaned, c);
                    }
                }

                if (lineEnd < buf.size)
                    InlineVec::Append(cleaned, (uint8_t)'\n');
            }

            pos = lineEnd + 1;
            if (pos > buf.size)
                break;
        }

        buf.size = 0;
        for (uint32_t i = 0; i < cleaned.size; ++i)
            InlineVec::Append(buf, cleaned.data[i]);
    };

    cleanupWhitespace(reindented);

    // ─── Stage 3: try the edit and validate with tree-sitter ───────────────

    if (force)
    {
        LOG("INFO: --force: skipping tree-sitter validation and re-indentation");
        if (dryRun)
        {
            LOG("OK: --dry-run: matched %u bytes at byte %u, would write %u bytes",
                oldText.size, matchPos, newText.size);
            return true;
        }
        uint64_t totalSize = source.size - oldText.size + newText.size;
        span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
        MemCpy(output.data, source.data, matchPos);
        MemCpy(output.data + matchPos, newText.data, newText.size);
        MemCpy(output.data + matchPos + newText.size, source.data + matchPos + oldText.size,
               source.size - (matchPos + oldText.size));

        SaveBackup(filePath, alloc);
        file_handle wfh = FileOpen(filePath, FileOpenMode::Write);
        if (!FileValid(wfh))
        {
            LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
            return false;
        }
        uint32_t written = FileWrite(wfh, (uint32_t)totalSize, output.data);
        FileClose(wfh);
        LOG("OK: edit committed (forced)");

        if (true) // always show structural diff
        {
            source_language lang = DetectLanguage(filePath);
            const TSLanguage *grammar = GrammarForLanguage(lang);
            if (grammar)
            {
                TSTree *beforeTree = ParseSource(source, grammar);
                TSNode beforeRoot = ts_tree_root_node(beforeTree);
                inline_vec<block_info, 256> beforeBlocks{};
                CollectBlockNodes(beforeRoot, beforeBlocks, 0, true, source, lang);
                TreeDelete(beforeTree);

                byteview outView{output.data, totalSize};
                TSTree *afterTree = ParseSource(outView, grammar);
                TSNode afterRoot = ts_tree_root_node(afterTree);
                inline_vec<block_info, 256> afterBlocks{};
                CollectBlockNodes(afterRoot, afterBlocks, 0, true, outView, lang);
                TreeDelete(afterTree);

                LOG("Structural diff:");
                auto typeLabel = [&](block_info &b) -> const char * {
                    char typeBuf[129];
                    uint32_t tn = (uint32_t)b.type.size > 128 ? 128 : (uint32_t)b.type.size;
                    MemCpy(typeBuf, b.type.data.data, tn);
                    typeBuf[tn] = 0;
                    return BlockTypeLabel(typeBuf);
                };
                // Removed blocks
                for (uint64_t bi = 0; bi < beforeBlocks.size; ++bi)
                {
                    block_info &bb = beforeBlocks.data.data[bi];
                    bool found = false;
                    for (uint64_t ai = 0; ai < afterBlocks.size; ++ai)
                    {
                        block_info &ab = afterBlocks.data.data[ai];
                        if (ab.startLine == bb.startLine && ab.endLine == bb.endLine &&
                            ab.type.size == bb.type.size &&
                            MemEq(ab.type.data.data, bb.type.data.data, bb.type.size))
                        {
                            found = true;
                            if (ab.name.size != bb.name.size ||
                                (ab.name.size > 0 && !MemEq(ab.name.data.data, bb.name.data.data, bb.name.size)) ||
                                (ab.endByte - ab.startByte) != (bb.endByte - bb.startByte))
                            {
                                const char *label = typeLabel(bb);
                                if (bb.name.size > 0)
                                    LOG("  ~ %u-%u %7s %.*s", bb.startLine + 1, bb.endLine + 1, label,
                                        (int)bb.name.size, bb.name.data.data);
                                else
                                    LOG("  ~ %u-%u %7s", bb.startLine + 1, bb.endLine + 1, label);
                            }
                            break;
                        }
                    }
                    if (!found)
                    {
                        const char *label = typeLabel(bb);
                        if (bb.name.size > 0)
                            LOG("  - %u-%u %7s %.*s", bb.startLine + 1, bb.endLine + 1, label,
                                (int)bb.name.size, bb.name.data.data);
                        else
                            LOG("  - %u-%u %7s", bb.startLine + 1, bb.endLine + 1, label);
                    }
                }
                // Added blocks
                for (uint64_t ai = 0; ai < afterBlocks.size; ++ai)
                {
                    block_info &ab = afterBlocks.data.data[ai];
                    bool found = false;
                    for (uint64_t bi = 0; bi < beforeBlocks.size; ++bi)
                    {
                        block_info &bb = beforeBlocks.data.data[bi];
                        if (ab.startLine == bb.startLine && ab.endLine == bb.endLine &&
                            ab.type.size == bb.type.size &&
                            MemEq(ab.type.data.data, bb.type.data.data, bb.type.size))
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        const char *label = typeLabel(ab);
                        if (ab.name.size > 0)
                            LOG("  + %u-%u %7s %.*s", ab.startLine + 1, ab.endLine + 1, label,
                                (int)ab.name.size, ab.name.data.data);
                        else
                            LOG("  + %u-%u %7s", ab.startLine + 1, ab.endLine + 1, label);
                    }
                }
            }
        }
        return true;
    }

    // ─── Pre-existing error detection ─────────────────────────────────
    inline_vec<uint32_t, 64> preExistingErrors{};
    {
        const TSLanguage *origGrammar = GrammarForLanguage(DetectLanguage(filePath));
        if (origGrammar)
        {
            TSTree *origTree = ParseSource(source, origGrammar);
            TSNode origRoot = ts_tree_root_node(origTree);
            auto collectErrors = [&](auto &self, TSNode n) -> void {
                if (ts_node_is_error(n) || ts_node_is_missing(n))
                {
                    node_range r = NodeRange(n);
                    InlineVec::Append(preExistingErrors, (r.startRow << 16) | (r.endRow & 0xFFFF));
                }
                uint32_t cc = ts_node_child_count(n);
                for (uint32_t i = 0; i < cc; ++i)
                    self(self, ts_node_child(n, i));
            };
            collectErrors(collectErrors, origRoot);
            // pre-existing errors are expected in real-world C++ files;
            // suppressing noise -- they're still tracked and new errors are caught below
            TreeDelete(origTree);
        }
    }

    auto isErrorPreExisting = [&](node_range r) -> bool {
        uint32_t fingerprint = (r.startRow << 16) | (r.endRow & 0xFFFF);
        for (uint32_t i = 0; i < preExistingErrors.size; ++i)
            if (preExistingErrors.data.data[i] == fingerprint)
                return true;
        return false;
    };

    auto tryEdit = [&](byteview candidate, const char *label) -> bool {
        uint64_t totalSize = source.size - oldText.size + candidate.size;
        span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
        MemCpy(output.data, source.data, matchPos);
        MemCpy(output.data + matchPos, candidate.data, candidate.size);
        MemCpy(output.data + matchPos + candidate.size, source.data + matchPos + oldText.size,
               source.size - (matchPos + oldText.size));

        const TSLanguage *grammar = GrammarForLanguage(DetectLanguage(filePath));
        if (!grammar)
        {
            // No grammar available — accept the edit. This is normal for
            // non-C/C++/Java/TS/JS files (shell scripts, markdown, etc).
        }
        else
        {
            byteview outView{output.data, totalSize};
            TSTree *tree = ParseSource(outView, grammar);
            TSNode root = ts_tree_root_node(tree);

            bool hasError = ts_node_has_error(root);
            if (hasError)
            {
                uint32_t errorCount = 0;
                uint32_t newErrorCount = 0;
                auto countErrors = [&](auto &self, TSNode n) -> void {
                    if (ts_node_is_error(n) || ts_node_is_missing(n))
                    {
                        ++errorCount;
                        node_range r = NodeRange(n);
                        if (!isErrorPreExisting(r))
                        {
                            ++newErrorCount;
                            LOG("INFO: syntax error near line %u (type: %s)", r.startRow + 1, ts_node_type(n));
                        }
                    }
                    uint32_t cc = ts_node_child_count(n);
                    for (uint32_t i = 0; i < cc && errorCount < 10; ++i)
                        self(self, ts_node_child(n, i));
                };
                countErrors(countErrors, root);

                if (newErrorCount > 0)
                {
                    LOG("ERROR: %s has %u NEW error(s) (%u pre-existing) — rejected — check indentation and braces", label, newErrorCount,
                        errorCount - newErrorCount);
                    TreeDelete(tree);
                    return false;
                }

                LOG("INFO: %s — %u pre-existing error(s), no new errors", label, errorCount);
            }
            else
            {
                LOG("OK: %s — tree-sitter parse clean", label);
            }
            TreeDelete(tree);
        }

            if (!dryRun)
            {
                SaveBackup(filePath, alloc);

                file_handle wfh = FileOpen(filePath, FileOpenMode::Write);
                if (!FileValid(wfh))
                {
                    LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
                    return false;
                }
                uint32_t written = FileWrite(wfh, (uint32_t)totalSize, output.data);
                FileClose(wfh);

                if (written != totalSize)
                {
                    LOG("ERROR: short write — file may be corrupt");
                    return false;
                }

                LOG("OK: edit committed");

                // ── Structural diff (--diff) ──
                if (true) // always show structural diff
                {
                    source_language lang = DetectLanguage(filePath);
                    const TSLanguage *grammar2 = GrammarForLanguage(lang);
                    if (grammar2)
                    {
                        TSTree *beforeTree = ParseSource(source, grammar2);
                        TSNode beforeRoot = ts_tree_root_node(beforeTree);
                        inline_vec<block_info, 256> beforeBlocks{};
                        CollectBlockNodes(beforeRoot, beforeBlocks, 0, true, source, lang);
                        TreeDelete(beforeTree);

                        byteview outView{output.data, totalSize};
                        TSTree *afterTree = ParseSource(outView, grammar2);
                        TSNode afterRoot = ts_tree_root_node(afterTree);
                        inline_vec<block_info, 256> afterBlocks{};
                        CollectBlockNodes(afterRoot, afterBlocks, 0, true, outView, lang);
                        TreeDelete(afterTree);

                        LOG("Structural diff:");
                        auto typeLabel = [&](block_info &b) -> const char * {
                            char typeBuf[129];
                            uint32_t tn = (uint32_t)b.type.size > 128 ? 128 : (uint32_t)b.type.size;
                            MemCpy(typeBuf, b.type.data.data, tn);
                            typeBuf[tn] = 0;
                            return BlockTypeLabel(typeBuf);
                        };
                        for (uint64_t bi = 0; bi < beforeBlocks.size; ++bi)
                        {
                            block_info &bb = beforeBlocks.data.data[bi];
                            bool found = false;
                            for (uint64_t ai = 0; ai < afterBlocks.size; ++ai)
                            {
                                block_info &ab = afterBlocks.data.data[ai];
                                if (ab.startLine == bb.startLine && ab.endLine == bb.endLine &&
                                    ab.type.size == bb.type.size &&
                                    MemEq(ab.type.data.data, bb.type.data.data, bb.type.size))
                                {
                                    found = true;
                                    if (ab.name.size != bb.name.size ||
                                        (ab.name.size > 0 && !MemEq(ab.name.data.data, bb.name.data.data, bb.name.size)) ||
                                        (ab.endByte - ab.startByte) != (bb.endByte - bb.startByte))
                                    {
                                        const char *label = typeLabel(bb);
                                        if (bb.name.size > 0)
                                            LOG("  ~ %u-%u %7s %.*s", bb.startLine + 1, bb.endLine + 1, label,
                                                (int)bb.name.size, bb.name.data.data);
                                        else
                                            LOG("  ~ %u-%u %7s", bb.startLine + 1, bb.endLine + 1, label);
                                    }
                                    break;
                                }
                            }
                            if (!found)
                            {
                                const char *label = typeLabel(bb);
                                if (bb.name.size > 0)
                                    LOG("  - %u-%u %7s %.*s", bb.startLine + 1, bb.endLine + 1, label,
                                        (int)bb.name.size, bb.name.data.data);
                                else
                                    LOG("  - %u-%u %7s", bb.startLine + 1, bb.endLine + 1, label);
                            }
                        }
                        for (uint64_t ai = 0; ai < afterBlocks.size; ++ai)
                        {
                            block_info &ab = afterBlocks.data.data[ai];
                            bool found = false;
                            for (uint64_t bi = 0; bi < beforeBlocks.size; ++bi)
                            {
                                block_info &bb = beforeBlocks.data.data[bi];
                                if (ab.startLine == bb.startLine && ab.endLine == bb.endLine &&
                                    ab.type.size == bb.type.size &&
                                    MemEq(ab.type.data.data, bb.type.data.data, bb.type.size))
                                {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                const char *label = typeLabel(ab);
                                if (ab.name.size > 0)
                                    LOG("  + %u-%u %7s %.*s", ab.startLine + 1, ab.endLine + 1, label,
                                        (int)ab.name.size, ab.name.data.data);
                                else
                                    LOG("  + %u-%u %7s", ab.startLine + 1, ab.endLine + 1, label);
                            }
                        }
                    }
                }
            }
            else
            {
                LOG("OK: --dry-run: matched %u bytes at byte %u, would write %u bytes",
                    oldText.size, matchPos, candidate.size);
            }
            return true;
        };

    byteview candidate{reindented.data.data, reindented.size};
    if (tryEdit(candidate, "re-indented"))
        return true;

    LOG("ERROR: all attempts failed — edit rejected, file unchanged. Try xcav read --raw to verify oldText");
    return false;
}

// ─── BlockExtract ───────────────────────────────────────────────────────────

auto BlockExtract(byteview srcFilePath, uint32_t srcLine, byteview dstFilePath, region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(srcFilePath, srcLine, alloc);
    if (!bl.tree)
        return false;

    byteview srcSource = bl.source;
    source_language lang = DetectLanguage(srcFilePath);

    LOG("INFO: moving %s block (lines %u-%u) to '%.*s'", bl.type, bl.range.startRow + 1,
        bl.range.endRow + 1, (int)dstFilePath.size, dstFilePath.data);

    // ── Collect includes/imports from source ──
    inline_vec<uint8_t, 16384> includeBlock{};
    {
        uint32_t pos = 0;
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
                for (uint32_t i = lineStart; i < lineEnd; ++i)
                    InlineVec::Append(includeBlock, srcSource.data[i]);
                InlineVec::Append(includeBlock, (uint8_t)'\n');
            }

            pos = lineEnd + 1;
            if (pos > srcSource.size)
                break;
        }
    }

    // ── Build new file content ──
    inline_vec<uint8_t, 65536> dstContent{};
    bool isCLike = (lang == source_language::C || lang == source_language::Cpp);

    if (isCLike)
    {
        const char *pragmaStr = "#pragma once\n";
        for (const char *s = pragmaStr; *s; ++s)
            InlineVec::Append(dstContent, (uint8_t)*s);
        InlineVec::Append(dstContent, (uint8_t)'\n');
    }

    for (uint32_t i = 0; i < includeBlock.size; ++i)
        InlineVec::Append(dstContent, includeBlock.data[i]);
    if (includeBlock.size > 0)
        InlineVec::Append(dstContent, (uint8_t)'\n');

    // Determine if block is inside a namespace
    inline_string<256> nsName{};
    {
        TSNode parent = ts_node_parent(bl.block);
        while (!ts_node_is_null(parent))
        {
            const char *ptype = ts_node_type(parent);
            if (StrEq(ptype, "namespace_definition"))
            {
                auto name = NodeName(parent, srcSource);
                if (name.size > 0)
                {
                    for (uint32_t i = 0; i < name.size; ++i)
                        InlineVec::Append(nsName, name.data[i]);
                }
                break;
            }
            parent = ts_node_parent(parent);
        }
    }

    // Namespace open
    if (nsName.size > 0)
    {
        const char *nsStr = "namespace ";
        for (const char *s = nsStr; *s; ++s)
            InlineVec::Append(dstContent, (uint8_t)*s);
        for (uint32_t i = 0; i < nsName.size; ++i)
            InlineVec::Append(dstContent, nsName.data[i]);
        InlineVec::Append(dstContent, (uint8_t)'\n');
        InlineVec::Append(dstContent, (uint8_t)'{');
        InlineVec::Append(dstContent, (uint8_t)'\n');
    }

    // Block text (re-indented to 0 since top level or namespace level)
    uint32_t blockLen = bl.range.endByte - bl.range.startByte;
    byteview blockText{srcSource.data + bl.range.startByte, blockLen};

    uint32_t baseIndent = 0;
    {
        uint32_t lineStart = LineStartOffset(srcSource, bl.range.startRow);
        while (lineStart + baseIndent < bl.range.startByte &&
               (srcSource.data[lineStart + baseIndent] == ' ' || srcSource.data[lineStart + baseIndent] == '\t'))
            ++baseIndent;
    }
    uint32_t targetIndent = nsName.size > 0 ? 4 : 0;

    // Re-indent and write block
    {
        uint32_t pos = 0;
        while (pos < blockText.size)
        {
            uint32_t lineIndent = 0;
            while (pos + lineIndent < blockText.size &&
                   (blockText.data[pos + lineIndent] == ' ' || blockText.data[pos + lineIndent] == '\t'))
                ++lineIndent;

            int32_t relativeIndent = (int32_t)lineIndent - (int32_t)baseIndent;
            if (relativeIndent < 0)
                relativeIndent = 0;
            uint32_t newIndent = targetIndent + (uint32_t)relativeIndent;

            for (uint32_t j = 0; j < newIndent; ++j)
                InlineVec::Append(dstContent, (uint8_t)' ');

            pos += lineIndent;
            while (pos < blockText.size && blockText.data[pos] != '\n')
            {
                InlineVec::Append(dstContent, blockText.data[pos]);
                ++pos;
            }
            if (pos < blockText.size && blockText.data[pos] == '\n')
            {
                InlineVec::Append(dstContent, (uint8_t)'\n');
                ++pos;
            }
        }
    }

    // Namespace close
    if (nsName.size > 0)
    {
        InlineVec::Append(dstContent, (uint8_t)'\n');
        InlineVec::Append(dstContent, (uint8_t)'}');
        InlineVec::Append(dstContent, (uint8_t)' ');
        const char *comment = "// namespace ";
        for (const char *s = comment; *s; ++s)
            InlineVec::Append(dstContent, (uint8_t)*s);
        for (uint32_t i = 0; i < nsName.size; ++i)
            InlineVec::Append(dstContent, nsName.data[i]);
        InlineVec::Append(dstContent, (uint8_t)'\n');
    }

    TreeDelete(bl.tree);

    // ── Write new file ──
    span<uint8_t> dstPathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, dstFilePath.size + 1);
    MemCpy(dstPathBuf.data, dstFilePath.data, dstFilePath.size);
    dstPathBuf.data[dstFilePath.size] = 0;

    file_handle dstFh = FileOpen(byteview{dstPathBuf.data, dstFilePath.size}, FileOpenMode::Write);
    if (!FileValid(dstFh))
    {
        LOG("ERROR: cannot create '%.*s'", (int)dstFilePath.size, dstFilePath.data);
        return false;
    }
    FileWrite(dstFh, (uint32_t)dstContent.size, dstContent.data.data);
    FileClose(dstFh);

    // ── Remove block from source and add include/import ──
    inline_vec<uint8_t, 256> includeLine{};
    if (isCLike)
    {
        const char *incStr = "#include \"";
        for (const char *s = incStr; *s; ++s)
            InlineVec::Append(includeLine, (uint8_t)*s);
        uint32_t nameStart = dstFilePath.size;
        while (nameStart > 0 && dstFilePath.data[nameStart - 1] != '/')
            --nameStart;
        for (uint32_t i = nameStart; i < dstFilePath.size; ++i)
            InlineVec::Append(includeLine, dstFilePath.data[i]);
        InlineVec::Append(includeLine, (uint8_t)'"');
        InlineVec::Append(includeLine, (uint8_t)'\n');
    }
    else if (lang == source_language::Java)
    {
        // Java: no import needed -- extracted class/method lives in its own file
        // or user will add package/import manually.
    }
    else if (lang == source_language::TypeScript || lang == source_language::JavaScript ||
             lang == source_language::Tsx)
    {
        // TS/JS: extract to module -- no automatic import added.
    }

    // Find where existing includes/imports end in source
    uint32_t srcIncludeEnd = 0;
    {
        uint32_t pos = 0;
        while (pos < srcSource.size)
        {
            uint32_t ls = pos;
            while (ls < srcSource.size && (srcSource.data[ls] == ' ' || srcSource.data[ls] == '\t'))
                ++ls;
            uint32_t le = ls;
            while (le < srcSource.size && srcSource.data[le] != '\n')
                ++le;

            bool isInc = false;
            if (lang == source_language::C || lang == source_language::Cpp)
            {
                if (ls + 1 < le && srcSource.data[ls] == '#')
                {
                    uint32_t p = ls + 1;
                    while (p < le && (srcSource.data[p] == ' ' || srcSource.data[p] == '\t'))
                        ++p;
                    if (p + 7 <= le && MemEq(srcSource.data + p, "include", 7))
                        isInc = true;
                }
            }
            else if (lang == source_language::Java)
            {
                if (ls + 6 < le && MemEq(srcSource.data + ls, "import", 6))
                    isInc = true;
            }
            else if (lang == source_language::TypeScript || lang == source_language::JavaScript ||
                     lang == source_language::Tsx)
            {
                if (ls + 6 < le && MemEq(srcSource.data + ls, "import", 6))
                    isInc = true;
            }

            if (isInc)
                srcIncludeEnd = le + 1;

            pos = le + 1;
            if (pos > srcSource.size)
                break;
        }
    }

    // Build source output: remove block + add include
    uint32_t cutStart = bl.range.startByte;
    uint32_t cutEnd = bl.range.endByte;

    while (cutStart > 0 && (srcSource.data[cutStart - 1] == ' ' || srcSource.data[cutStart - 1] == '\t' ||
                            srcSource.data[cutStart - 1] == '\n'))
    {
        --cutStart;
        if (srcSource.data[cutStart] == '\n')
            break;
    }

    // Eat preceding // comment lines
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
            while (commentCut > 0 &&
                   (srcSource.data[commentCut - 1] == ' ' || srcSource.data[commentCut - 1] == '\t' ||
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

    uint64_t extraSize = includeLine.size;
    uint64_t srcTotalSize = srcSource.size - (cutEnd - cutStart) + extraSize;
    span<uint8_t> srcOutput = RegionAlloc::AllocArray<uint8_t>(alloc, srcTotalSize);

    uint64_t outPos = 0;
    MemCpy(srcOutput.data + outPos, srcSource.data, srcIncludeEnd);
    outPos += srcIncludeEnd;
    for (uint32_t i = 0; i < includeLine.size; ++i)
        srcOutput.data[outPos++] = includeLine.data[i];
    MemCpy(srcOutput.data + outPos, srcSource.data + srcIncludeEnd, cutStart - srcIncludeEnd);
    outPos += cutStart - srcIncludeEnd;
    MemCpy(srcOutput.data + outPos, srcSource.data + cutEnd, srcSource.size - cutEnd);
    outPos += srcSource.size - cutEnd;
    ASSERT(outPos == srcTotalSize);

    SaveBackup(srcFilePath, alloc);

    file_handle srcFh = FileOpen(srcFilePath, FileOpenMode::Write);
    if (!FileValid(srcFh))
    {
        LOG("ERROR: cannot write '%.*s'", (int)srcFilePath.size, srcFilePath.data);
        return false;
    }
    uint32_t written = FileWrite(srcFh, (uint32_t)srcTotalSize, srcOutput.data);
    FileClose(srcFh);

    if (written != srcTotalSize)
    {
        LOG("ERROR: short write on extract — file may be corrupt");
        return false;
    }

    LOG("OK: wrote '%.*s', updated '%.*s'", (int)dstFilePath.size,
        dstFilePath.data, (int)srcFilePath.size, srcFilePath.data);
    return true;
}

// ─── TidyFile ───────────────────────────────────────────────────────────────

auto TidyFile(byteview filePath, region_alloc &alloc) -> bool
{
    if (!IsRegularFile(filePath, alloc))
    {
        LOG("ERROR: not a regular file '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }

    file_handle fh = FileOpen(filePath, FileOpenMode::Read);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot open '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    byteview source = FileReadFully(alloc, fh);
    FileClose(fh);

    // Build re-indented output with brace-counting indentation
    inline_vec<uint8_t, 65536> out{};
    int32_t indentLevel = 0;
    uint32_t lineStart = 0;

    while (lineStart < source.size)
    {
        uint32_t lineEnd = lineStart;
        while (lineEnd < source.size && source.data[lineEnd] != '\n')
            ++lineEnd;

        uint32_t lineIndent = 0;
        while (lineStart + lineIndent < lineEnd &&
               (source.data[lineStart + lineIndent] == ' ' || source.data[lineStart + lineIndent] == '\t'))
            ++lineIndent;

        uint32_t contentStart = lineStart + lineIndent;
        bool isBlank = (contentStart >= lineEnd);

        if (isBlank)
        {
            InlineVec::Append(out, (uint8_t)'\n');
        }
        else
        {
            uint8_t firstChar = source.data[contentStart];

            int32_t lineIndentLevel = indentLevel;
            if (firstChar == '#')
                lineIndentLevel = 0;
            else if (firstChar == '}' && lineIndentLevel > 0)
                --lineIndentLevel;

            uint32_t newIndent = (uint32_t)Max((int32_t)0, lineIndentLevel) * 4;
            for (uint32_t j = 0; j < newIndent; ++j)
                InlineVec::Append(out, (uint8_t)' ');

            for (uint32_t j = contentStart; j < lineEnd; ++j)
                InlineVec::Append(out, source.data[j]);
            if (lineEnd < source.size)
                InlineVec::Append(out, (uint8_t)'\n');

            int32_t braceDelta = 0;
            for (uint32_t j = contentStart; j < lineEnd; ++j)
            {
                if (source.data[j] == '{')
                    ++braceDelta;
                else if (source.data[j] == '}')
                    --braceDelta;
            }
            if (firstChar == '}')
                braceDelta += 1;

            indentLevel += braceDelta;
        }

        lineStart = lineEnd + 1;
        if (lineStart > source.size)
            break;
    }

    // ── Whitespace cleanup ──
    {
        inline_vec<uint8_t, 65536> cleaned{};
        uint32_t pos = 0;
        uint32_t blankRun = 0;

        while (pos < out.size)
        {
            uint32_t clEnd = pos;
            while (clEnd < out.size && out.data[clEnd] != '\n')
                ++clEnd;

            bool isBlank = true;
            for (uint32_t j = pos; j < clEnd; ++j)
            {
                if (out.data[j] != ' ' && out.data[j] != '\t')
                {
                    isBlank = false;
                    break;
                }
            }

            if (isBlank)
            {
                ++blankRun;
                if (blankRun <= 1)
                    InlineVec::Append(cleaned, (uint8_t)'\n');
            }
            else
            {
                blankRun = 0;
                uint32_t contentEnd = clEnd;
                while (contentEnd > pos &&
                       (out.data[contentEnd - 1] == ' ' || out.data[contentEnd - 1] == '\t'))
                    --contentEnd;

                for (uint32_t j = pos; j < contentEnd; ++j)
                {
                    uint8_t c = out.data[j];
                    if (c == '\t')
                    {
                        InlineVec::Append(cleaned, (uint8_t)' ');
                        InlineVec::Append(cleaned, (uint8_t)' ');
                        InlineVec::Append(cleaned, (uint8_t)' ');
                        InlineVec::Append(cleaned, (uint8_t)' ');
                    }
                    else
                    {
                        InlineVec::Append(cleaned, c);
                    }
                }

                if (clEnd < out.size)
                    InlineVec::Append(cleaned, (uint8_t)'\n');
            }

            pos = clEnd + 1;
            if (pos > out.size)
                break;
        }

        out.size = 0;
        for (uint32_t i = 0; i < cleaned.size; ++i)
            InlineVec::Append(out, cleaned.data[i]);
    }

    // ── Write back ──
    SaveBackup(filePath, alloc);

    fh = FileOpen(filePath, FileOpenMode::Write);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    uint32_t written = FileWrite(fh, (uint32_t)out.size, out.data.data);
    FileClose(fh);

    if (written != out.size)
    {
        LOG("ERROR: short write on tidy — file may be corrupt");
        return false;
    }

    LOG("OK: re-indented file");
    return true;
}

} // namespace nyla
