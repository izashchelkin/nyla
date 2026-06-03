#include "xcav/replace.h"

#include "xcav/backup.h"
#include "xcav/block_query.h"
#include "xcav/text_util.h"
#include "xcav/tree_sitter_util.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

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
           (source.data[cutStart - 1] == ' ' || source.data[cutStart - 1] == '\t' || source.data[cutStart - 1] == '\n'))
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
            while (contentStart < commentCut && (source.data[contentStart] == ' ' || source.data[contentStart] == '\t'))
                ++contentStart;
            bool isComment = (contentStart + 1 < commentCut && source.data[contentStart] == '/' &&
                              source.data[contentStart + 1] == '/');
            if (!isComment)
                break;
            commentCut = lineStart;
            while (commentCut > 0 && (source.data[commentCut - 1] == ' ' || source.data[commentCut - 1] == '\t' ||
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

} // namespace nyla
