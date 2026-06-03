#include "xcav/insert.h"

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

// ─── InsertBlock ───────────────────────────────────────────────────────────

// Internal: insert content before (beforeBlock=true) or after a structural block.
static auto InsertBlock(byteview filePath, uint32_t blockLine, byteview contentFilePath, bool beforeBlock,
                        region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    const char *dir = beforeBlock ? "before" : "after";
    LOG("INFO: inserting %s %s block (lines %u-%u)", dir, bl.type, bl.range.startRow + 1, bl.range.endRow + 1);

    // Read content from file
    file_handle contentFh = FileOpen(contentFilePath, FileOpenMode::Read);
    if (!FileValid(contentFh))
    {
        TreeDelete(bl.tree);
        LOG("ERROR: cannot read content file '%.*s'", (int)contentFilePath.size, contentFilePath.data);
        return false;
    }
    byteview newText = FileReadFully(alloc, contentFh);
    FileClose(contentFh);

    // Determine base indent from the target block
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

    // Ensure indentedNew ends with newline if it has content, and add a
    // newline before the insertion to separate from the target block.
    if (indentedNew.size > 0 && indentedNew.data.data[indentedNew.size - 1] != '\n')
        InlineVec::Append(indentedNew, (uint8_t)'\n');

    TreeDelete(bl.tree);

    // Compute insertion point (byte offset within source)
    uint32_t insertByte;
    if (beforeBlock)
    {
        // Insert before the block — eat leading whitespace to put insertion
        // right before the block's opening line, with a blank separator.
        insertByte = bl.range.startByte;
        while (insertByte > 0 && (source.data[insertByte - 1] == ' ' || source.data[insertByte - 1] == '\t' ||
                                  source.data[insertByte - 1] == '\n'))
            --insertByte;
    }
    else
    {
        // Insert after the block — after the closing character and any
        // trailing newline.
        insertByte = bl.range.endByte;
        if (insertByte < source.size && source.data[insertByte] == '\n')
            ++insertByte;
    }

    // Build output: before + new content + after.
    // Add a leading newline for separation from preceding content.
    uint64_t totalSize = insertByte + 1 + indentedNew.size + (source.size - insertByte);
    span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    MemCpy(output.data, source.data, insertByte);
    output.data[insertByte] = '\n';
    MemCpy(output.data + insertByte + 1, indentedNew.data.data, indentedNew.size);
    MemCpy(output.data + insertByte + 1 + indentedNew.size, source.data + insertByte, source.size - insertByte);

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

    LOG("OK: inserted %s block at line %u", dir, blockLine + 1);
    return true;
}

auto InsertBeforeBlock(byteview filePath, uint32_t blockLine, byteview contentFilePath, region_alloc &alloc) -> bool
{
    return InsertBlock(filePath, blockLine, contentFilePath, true, alloc);
}

auto InsertAfterBlock(byteview filePath, uint32_t blockLine, byteview contentFilePath, region_alloc &alloc) -> bool
{
    return InsertBlock(filePath, blockLine, contentFilePath, false, alloc);
}

} // namespace nyla
