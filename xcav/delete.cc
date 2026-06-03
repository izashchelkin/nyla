#include "xcav/delete.h"

#include "xcav/backup.h"
#include "xcav/block_query.h"
#include "xcav/text_util.h"

#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{
auto DeleteBlock(byteview filePath, uint32_t blockLine, region_alloc &alloc) -> bool
{
    block_loc bl = LocateBlock(filePath, blockLine, alloc);
    if (!bl.tree)
        return false;

    // Save the block type before freeing the tree — ts_node_type() pointer
    // is owned by the tree and becomes invalid after TreeDelete.
    const char *blockType = bl.type;

    TreeDelete(bl.tree);

    // Remove block + cleanup surrounding blank lines and leading comments.
    byteview source = bl.source;
    // Extend to full line boundaries: tree-sitter ranges may start mid-line (after modifiers)
    uint32_t cutStart = LineStartOffset(source, bl.range.startRow);
    uint32_t cutEnd = bl.range.endByte;
    // For struct/enum/class/union specifiers/declarations,
    // tree-sitter does NOT include the trailing ';' in the block range.
    // Eat it so we don't leave orphaned semicolons.
    {
        bool isTypeDecl = StrEq(blockType, "struct_specifier") || StrEq(blockType, "enum_specifier") ||
                          StrEq(blockType, "class_specifier") || StrEq(blockType, "class_declaration") ||
                          StrEq(blockType, "enum_declaration") || StrEq(blockType, "union_specifier");
        if (isTypeDecl)
        {
            // Skip whitespace and newlines after the block's closing '}'
            uint32_t pos = cutEnd;
            while (pos < source.size &&
                   (source.data[pos] == ' ' || source.data[pos] == '\t' || source.data[pos] == '\n'))
                ++pos;
            if (pos < source.size && source.data[pos] == ';')
                cutEnd = pos + 1; // include the ';'
        }
    }

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

    LOG("OK: deleted %s block (lines %u-%u)", blockType, bl.range.startRow + 1, bl.range.endRow + 1);
    return true;
}

} // namespace nyla
