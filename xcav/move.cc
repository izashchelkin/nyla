#include "xcav/move.h"

#include "xcav/backup.h"
#include "xcav/block_query.h"
#include "xcav/text_util.h"

#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

auto MoveBlock(byteview filePath, uint32_t blockLine, uint32_t destLine, region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    byteview source = bl.source;

    // ── Extend deletion range to full line boundaries ──
    // Tree-sitter node ranges may start after the line indent and end before
    // the trailing newline. Extending prevents orphaned whitespace and blank lines.
    uint32_t delStart = LineStartOffset(source, bl.range.startRow);
    uint32_t delEnd = bl.range.endByte;
    if (delEnd < source.size && source.data[delEnd] == '\n')
        ++delEnd;

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
        totalSize = delStart + (destOffset - delEnd) + reindented.size + (source.size - destOffset);
        output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);

        uint64_t outPos = 0;
        MemCpy(output.data + outPos, source.data, delStart);
        outPos += delStart;
        MemCpy(output.data + outPos, source.data + delEnd, destOffset - delEnd);
        outPos += destOffset - delEnd;
        MemCpy(output.data + outPos, reindented.data.data, reindented.size);
        outPos += reindented.size;
        MemCpy(output.data + outPos, source.data + destOffset, source.size - destOffset);
        outPos += source.size - destOffset;
        ASSERT(outPos == totalSize);
    }
    else if (bl.range.startByte >= destOffset)
    {
        totalSize = destOffset + reindented.size + (delStart - destOffset) + (source.size - delEnd);
        output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);

        uint64_t outPos = 0;
        MemCpy(output.data + outPos, source.data, destOffset);
        outPos += destOffset;
        MemCpy(output.data + outPos, reindented.data.data, reindented.size);
        outPos += reindented.size;
        MemCpy(output.data + outPos, source.data + destOffset, delStart - destOffset);
        outPos += delStart - destOffset;
        MemCpy(output.data + outPos, source.data + delEnd, source.size - delEnd);
        outPos += source.size - delEnd;
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

    LOG("OK: moved %s block (lines %u-%u) to after line %u", bl.type, bl.range.startRow + 1, bl.range.endRow + 1,
        destLine + 1);
    return true;
}

} // namespace nyla
