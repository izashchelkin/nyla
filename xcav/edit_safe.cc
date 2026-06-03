#include "xcav/edit_safe.h"

#include "xcav/backup.h"
#include "xcav/block_query.h"
#include "xcav/language.h"
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
auto EditSafe(byteview filePath, byteview oldFile, byteview newFile, region_alloc &alloc, bool dryRun) -> bool
{
    // ── Read old and new text from temp files ──
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

    if (oldText.size == 0)
    {
        LOG("ERROR: oldText cannot be empty");
        return false;
    }

    // ── Normalize Unicode (em-dash→--, smart quotes→ASCII, etc.) ──
    oldText = NormalizeForEdit(oldText, alloc, false).text;
    newText = NormalizeForEdit(newText, alloc, false).text;

    // ── Read target file ──
    file_handle fh = FileOpen(filePath, FileOpenMode::Read);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot open '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    byteview source = FileReadFully(alloc, fh);
    FileClose(fh);

    // Normalize source for matching (same Unicode normalization as oldText).
    byteview normSource = NormalizeForEdit(source, alloc, true).text;

    // ── Split oldText into lines, extract content ──
    struct line_info
    {
        uint32_t start;  // byte offset in oldText
        uint32_t end;    // byte past last char (before \n)
        uint32_t cstart; // content start (after leading whitespace)
        uint32_t cend;   // content end (before trailing whitespace)
        bool blank;
    };
    span<line_info> oldLines = RegionAlloc::AllocArray<line_info>(alloc, oldText.size);
    uint32_t oldLineCount = 0;
    {
        uint32_t p = 0;
        while (p < oldText.size)
        {
            uint32_t ls = p;
            while (p < oldText.size && oldText.data[p] != '\n')
                ++p;
            uint32_t le = p;

            // Compute content range (strip leading/trailing whitespace)
            uint32_t cs = ls;
            while (cs < le && (oldText.data[cs] == ' ' || oldText.data[cs] == '\t'))
                ++cs;
            uint32_t ce = le;
            while (ce > cs && (oldText.data[ce - 1] == ' ' || oldText.data[ce - 1] == '\t'))
                --ce;

            bool blank = (cs >= ce);
            oldLines.data[oldLineCount++] = {ls, le, cs, ce, blank};

            if (p < oldText.size && oldText.data[p] == '\n')
                ++p;
        }
    }

    if (oldLineCount == 0)
    {
        LOG("ERROR: oldText has no lines");
        return false;
    }

    // Strip leading/trailing blank lines -- artifacts from copy-paste.
    uint32_t firstContent = 0;
    while (firstContent < oldLineCount && oldLines.data[firstContent].blank)
        ++firstContent;
    uint32_t lastContent = oldLineCount;
    while (lastContent > firstContent && oldLines.data[lastContent - 1].blank)
        --lastContent;
    uint32_t contentLineCount = lastContent - firstContent;
    if (contentLineCount == 0)
    {
        LOG("ERROR: oldText is all blank lines");
        return false;
    }

    // ── Split normalized source into lines ──
    struct src_line
    {
        uint32_t start;        // byte offset in normSource
        uint32_t end;          // byte past \n or EOF
        uint32_t contentStart; // byte offset of first non-whitespace char
        uint32_t indent;       // leading whitespace in display columns (tab=4)
        bool blank;
    };
    span<src_line> srcLines = RegionAlloc::AllocArray<src_line>(alloc, normSource.size / 2 + 16);
    uint32_t srcLineCount = 0;
    {
        uint32_t p = 0;
        while (p < normSource.size)
        {
            uint32_t ls = p;
            uint32_t indent = 0;
            while (p < normSource.size && (normSource.data[p] == ' ' || normSource.data[p] == '\t'))
            {
                indent += (normSource.data[p] == '\t') ? 4 : 1;
                ++p;
            }
            uint32_t cs = p; // byte offset of first content character
            while (p < normSource.size && normSource.data[p] != '\n')
                ++p;
            uint32_t le = p;
            bool blank = (cs >= le);
            srcLines.data[srcLineCount++] = {ls, le + (p < normSource.size ? 1 : 0), cs, indent, blank};
            if (p < normSource.size && normSource.data[p] == '\n')
                ++p;
        }
    }

    // ── Find matching line sequence ──
    uint32_t matchLine = 0;
    bool found = false;
    uint32_t matchCount = 0;
    for (uint32_t si = 0; si + contentLineCount <= srcLineCount; ++si)
    {
        // Skip leading blank source lines that would match stripped old blanks.
        uint32_t sPos = si;
        uint32_t oPos = firstContent;
        bool ok = true;
        while (oPos < lastContent)
        {
            line_info &ol = oldLines.data[oPos];
            if (sPos >= srcLineCount)
            {
                ok = false;
                break;
            }
            src_line &sl = srcLines.data[sPos];

            if (ol.blank)
            {
                // Old blank line requires source blank line.
                if (!sl.blank)
                {
                    ok = false;
                    break;
                }
            }
            else
            {
                // Compare stripped content.
                uint32_t oldLen = ol.cend - ol.cstart;
                uint32_t srcContentLen = sl.end - sl.start;
                if (sl.end > sl.start && normSource.data[sl.end - 1] == '\n')
                    srcContentLen = sl.end - sl.start - 1;
                uint32_t srcCS = sl.contentStart;
                uint32_t srcCE =
                    sl.contentStart +
                    (srcContentLen > (sl.contentStart - sl.start) ? srcContentLen - (sl.contentStart - sl.start) : 0);
                // Strip trailing whitespace from source line content
                while (srcCE > srcCS && (normSource.data[srcCE - 1] == ' ' || normSource.data[srcCE - 1] == '\t'))
                    --srcCE;
                uint32_t srcLen = srcCE - srcCS;

                if (oldLen != srcLen || !MemEq(oldText.data + ol.cstart, normSource.data + srcCS, oldLen))
                {
                    ok = false;
                    break;
                }
            }
            ++sPos;
            ++oPos;
        }
        if (ok)
        {
            matchLine = si;
            found = true;
            ++matchCount;
        }
    }

    if (!found)
    {
        LOG("ERROR: oldText not found in file");

        // Show what edit was looking for (stripped content, first 5 lines)
        uint32_t previewLines = contentLineCount;
        if (previewLines > 5)
            previewLines = 5;
        LOG("  Looking for (%u lines):", contentLineCount);
        for (uint32_t i = 0; i < previewLines; ++i)
        {
            line_info &ol = oldLines.data[firstContent + i];
            uint32_t clen = ol.cend - ol.cstart;
            if (clen > 0)
                FileWriteFmt(GetStderr(), "    %.*s\n"_s, (int)clen, oldText.data + ol.cstart);
            else
                FileWriteFmt(GetStderr(), "    (blank)\n"_s);
        }
        if (contentLineCount > 5)
            LOG("    ... (%u more lines)", contentLineCount - 5);

        // Partial match: find source lines matching first content line of oldText,
        // then show where the full block diverges at each partial match location.
        uint32_t partialCount = 0;
        uint32_t shownDivergences = 0;
        {
            line_info &fol = oldLines.data[firstContent];
            uint32_t firstLen = fol.cend - fol.cstart;
            for (uint32_t si = 0; si + contentLineCount <= srcLineCount; ++si)
            {
                src_line &sl = srcLines.data[si];
                if (sl.blank)
                    continue;
                uint32_t sCS = sl.contentStart;
                uint32_t sCE = sl.end;
                if (sCE > sl.start && normSource.data[sCE - 1] == '\n')
                    --sCE;
                while (sCE > sCS && (normSource.data[sCE - 1] == ' ' || normSource.data[sCE - 1] == '\t'))
                    --sCE;
                uint32_t sLen = sCE - sCS;
                if (sLen != firstLen || !MemEq(oldText.data + fol.cstart, normSource.data + sCS, firstLen))
                    continue;

                // First line matches at source line si -- check subsequent lines to find divergence.
                ++partialCount;
                if (shownDivergences >= 3)
                    continue;

                uint32_t checkSi = si;
                uint32_t checkOi = firstContent;
                bool diverged = false;
                while (checkOi < lastContent && checkSi < srcLineCount)
                {
                    line_info &ol = oldLines.data[checkOi];
                    src_line &csl = srcLines.data[checkSi];
                    if (ol.blank)
                    {
                        if (!csl.blank)
                        {
                            diverged = true;
                            FileWriteFmt(GetStderr(), "  At source line %u: expected blank, found '%.*s'\n"_s,
                                         checkSi + 1, (int)(csl.end - csl.start > 40 ? 40 : csl.end - csl.start),
                                         normSource.data + csl.start);
                            break;
                        }
                    }
                    else
                    {
                        uint32_t oldLen = ol.cend - ol.cstart;
                        uint32_t cslContentLen = csl.end - csl.start;
                        if (csl.end > csl.start && normSource.data[csl.end - 1] == '\n')
                            cslContentLen = csl.end - csl.start - 1;
                        uint32_t cCS = csl.contentStart;
                        uint32_t cCE = csl.contentStart + (cslContentLen > (csl.contentStart - csl.start)
                                                               ? cslContentLen - (csl.contentStart - csl.start)
                                                               : 0);
                        while (cCE > cCS && (normSource.data[cCE - 1] == ' ' || normSource.data[cCE - 1] == '\t'))
                            --cCE;
                        uint32_t cslLen = cCE - cCS;
                        if (oldLen != cslLen || !MemEq(oldText.data + ol.cstart, normSource.data + cCS, oldLen))
                        {
                            diverged = true;
                            FileWriteFmt(GetStderr(), "  At source line %u:"_s, checkSi + 1);
                            FileWriteFmt(GetStderr(), " expected '%.*s'"_s, (int)oldLen, oldText.data + ol.cstart);
                            FileWriteFmt(GetStderr(), " but file has '%.*s'\n"_s, (int)(cslLen > 40 ? 40 : cslLen),
                                         normSource.data + cCS);
                            break;
                        }
                    }
                    ++checkSi;
                    ++checkOi;
                }
                if (!diverged)
                {
                    FileWriteFmt(GetStderr(),
                                 "  At source line %u: first line matches but block is shorter than oldText\n"_s,
                                 si + 1);
                }
                ++shownDivergences;
            }
        }
        if (partialCount > 0)
            LOG("  First line matches at %u location(s) -- divergences shown above", partialCount);
        else
            LOG("  First line not found anywhere in file (wrong file or stale read?)");
        return false;
    }
    if (matchCount > 1)
    {
        LOG("ERROR: oldText matches %u times — ambiguous, add more context lines", matchCount);
        return false;
    }

    uint32_t matchEndLine = matchLine + contentLineCount;
    // Expand matchEndLine past trailing blank source lines.
    while (matchEndLine < srcLineCount && srcLines.data[matchEndLine].blank)
        ++matchEndLine;

    // ── Compute indentation from matched source lines ──
    // Find the base indent: the indent of the first non-blank matched line.
    uint32_t baseIndent = 0;
    for (uint32_t i = matchLine; i < matchEndLine; ++i)
    {
        if (!srcLines.data[i].blank)
        {
            baseIndent = srcLines.data[i].indent;
            break;
        }
    }

    // ── Split newText into lines and re-indent ──
    // newText's own minimum indent is subtracted, then baseIndent is added.
    struct new_line_info
    {
        uint32_t start;
        uint32_t end; // byte past \n or EOF
        uint32_t cstart;
        uint32_t cend;
        bool blank;
    };
    span<new_line_info> newLines = RegionAlloc::AllocArray<new_line_info>(alloc, newText.size);
    uint32_t newLineCount = 0;
    {
        uint32_t p = 0;
        while (p < newText.size)
        {
            uint32_t ls = p;
            while (p < newText.size && newText.data[p] != '\n')
                ++p;
            uint32_t le = p;

            uint32_t cs = ls;
            while (cs < le && (newText.data[cs] == ' ' || newText.data[cs] == '\t'))
                ++cs;
            uint32_t ce = le;
            while (ce > cs && (newText.data[ce - 1] == ' ' || newText.data[ce - 1] == '\t'))
                --ce;

            newLines.data[newLineCount++] = {ls, le, cs, ce, cs >= ce};

            if (p < newText.size && newText.data[p] == '\n')
                ++p;
        }
    }

    // Strip leading/trailing blanks from newText.
    uint32_t newFirst = 0;
    while (newFirst < newLineCount && newLines.data[newFirst].blank)
        ++newFirst;
    uint32_t newLast = newLineCount;
    while (newLast > newFirst && newLines.data[newLast - 1].blank)
        --newLast;

    // Compute min indent of newText content (non-blank lines).
    uint32_t newMinIndent = 0xFFFFFFFF;
    for (uint32_t i = newFirst; i < newLast; ++i)
    {
        if (!newLines.data[i].blank)
        {
            uint32_t ind = newLines.data[i].cstart - newLines.data[i].start;
            if (ind < newMinIndent)
                newMinIndent = ind;
        }
    }
    if (newMinIndent == 0xFFFFFFFF)
        newMinIndent = 0;

    // ── Build output ──
    // Source byte ranges from normalized srcLines.
    uint32_t matchByteStart = srcLines.data[matchLine].start;
    uint32_t matchByteEnd = (matchEndLine < srcLineCount) ? srcLines.data[matchEndLine].start : normSource.size;

    // Map to original source: find the corresponding bytes.
    // NormalizeForEdit with keepMapping=true gives normToOrig array.
    // We need to map matchByteStart/matchByteEnd back to original source offsets.
    normalized_edit_text normalizedSource = NormalizeForEdit(source, alloc, true);
    span<uint32_t> normToOrig = normalizedSource.normalizedToOriginal;

    uint32_t origMatchStart = 0;
    uint32_t origMatchEnd = source.size;
    if (normToOrig.data != nullptr)
    {
        if (matchByteStart < normalizedSource.text.size)
            origMatchStart = normToOrig.data[matchByteStart];
        else
            origMatchStart = source.size;

        if (matchByteEnd > 0 && matchByteEnd <= normalizedSource.text.size)
            origMatchEnd = normToOrig.data[matchByteEnd - 1] + 1;
        else if (matchByteEnd == 0)
            origMatchEnd = 0;
        else
            origMatchEnd = source.size;
    }
    else
    {
        origMatchStart = matchByteStart;
        origMatchEnd = matchByteEnd;
    }

    // Build re-indented newText.
    inline_vec<uint8_t, 65536> indentedNew{};
    for (uint32_t i = newFirst; i < newLast; ++i)
    {
        new_line_info &nl = newLines.data[i];
        if (nl.blank)
        {
            InlineVec::Append(indentedNew, (uint8_t)'\n');
        }
        else
        {
            uint32_t relIndent = (nl.cstart - nl.start) - newMinIndent;
            uint32_t totalIndent = baseIndent + relIndent;
            for (uint32_t j = 0; j < totalIndent; ++j)
                InlineVec::Append(indentedNew, (uint8_t)' ');
            for (uint32_t j = nl.cstart; j < nl.cend; ++j)
                InlineVec::Append(indentedNew, newText.data[j]);
            InlineVec::Append(indentedNew, (uint8_t)'\n');
        }
    }

    // ── Dry run ──
    if (dryRun)
    {
        LOG("OK: --dry-run: would replace %u bytes at byte %u with %u bytes", origMatchEnd - origMatchStart,
            origMatchStart, (uint32_t)indentedNew.size);
        return true;
    }

    // ── Build final output ──
    uint64_t totalSize = origMatchStart + indentedNew.size + (source.size - origMatchEnd);
    span<uint8_t> output = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    MemCpy(output.data, source.data, origMatchStart);
    MemCpy(output.data + origMatchStart, indentedNew.data.data, indentedNew.size);
    MemCpy(output.data + origMatchStart + indentedNew.size, source.data + origMatchEnd, source.size - origMatchEnd);

    // ── Write ──
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

    LOG("OK: replaced %u line%s at line %u", contentLineCount, contentLineCount == 1 ? "" : "s", matchLine + 1);
    return true;
}

} // namespace nyla
