#include "xcav/text_util.h"

namespace nyla
{

// ─── LineStartOffset ────────────────────────────────────────────────────────

auto LineStartOffset(byteview source, uint32_t line) -> uint32_t
{
    if (line == 0)
        return 0;
    uint32_t current = 0;
    for (uint32_t i = 0; i < source.size; ++i)
    {
        if (source.data[i] == '\n')
        {
            ++current;
            if (current == line && i + 1 < source.size)
                return i + 1;
        }
    }
    return (uint32_t)source.size;
}

// ─── LineEndOffset ──────────────────────────────────────────────────────────

auto LineEndOffset(byteview source, uint32_t line) -> uint32_t
{
    uint32_t start = LineStartOffset(source, line);
    uint32_t end = start;
    while (end < source.size && source.data[end] != '\n')
        ++end;
    return end;
}

// ─── LineIndent ─────────────────────────────────────────────────────────────

auto LineIndent(byteview source, uint32_t lineStart) -> uint32_t
{
    uint32_t count = 0;
    uint32_t i = lineStart;
    while (i < source.size && (source.data[i] == ' ' || source.data[i] == '\t'))
    {
        ++count;
        ++i;
    }
    return count;
}

// ─── StrEq ──────────────────────────────────────────────────────────────────

auto StrEq(const char *a, const char *b) -> bool
{
    while (*a && *b)
    {
        if (*a != *b)
            return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

// ─── IncludePath ────────────────────────────────────────────────────────────

auto IncludePath(byteview blockText) -> inline_string<128>
{
    inline_string<128> result{};
    uint32_t i = 0;
    while (i < blockText.size && blockText.data[i] != '"' && blockText.data[i] != '<')
        ++i;
    if (i < blockText.size)
    {
        uint8_t delim = blockText.data[i];
        ++i;
        while (i < blockText.size && blockText.data[i] != delim && blockText.data[i] != '\n' && result.size < 127)
        {
            InlineVec::Append(result, blockText.data[i]);
            ++i;
        }
    }
    return result;
}

// ─── NormalizeText ──────────────────────────────────────────────────────────

void NormalizeText(inline_vec<uint8_t, 65536> &buf)
{
    inline_vec<uint8_t, 65536> out{};
    for (uint32_t i = 0; i < buf.size; ++i)
    {
        uint8_t c = buf.data[i];
        // UTF-8 multi-byte sequences: check for known Unicode chars
        if (c >= 0xE2 && i + 2 < buf.size)
        {
            uint8_t c1 = buf.data[i + 1];
            uint8_t c2 = buf.data[i + 2];
            // U+2014 em-dash — (E2 80 94) → "--"
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x94)
            {
                InlineVec::Append(out, (uint8_t)'-');
                InlineVec::Append(out, (uint8_t)'-');
                i += 2;
                continue;
            }
            // U+2013 en-dash – (E2 80 93) → "-"
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x93)
            {
                InlineVec::Append(out, (uint8_t)'-');
                i += 2;
                continue;
            }
            // U+2192 right arrow → (E2 86 92) → "->"
            if (c == 0xE2 && c1 == 0x86 && c2 == 0x92)
            {
                InlineVec::Append(out, (uint8_t)'-');
                InlineVec::Append(out, (uint8_t)'>');
                i += 2;
                continue;
            }
            // U+2190 left arrow ← (E2 86 90) → "<-"
            if (c == 0xE2 && c1 == 0x86 && c2 == 0x90)
            {
                InlineVec::Append(out, (uint8_t)'<');
                InlineVec::Append(out, (uint8_t)'-');
                i += 2;
                continue;
            }
            // U+201C left double quote “ (E2 80 9C) → '"'
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x9C)
            {
                InlineVec::Append(out, (uint8_t)'"');
                i += 2;
                continue;
            }
            // U+201D right double quote ” (E2 80 9D) → '"'
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x9D)
            {
                InlineVec::Append(out, (uint8_t)'"');
                i += 2;
                continue;
            }
            // U+2018 left single quote ' (E2 80 98) → '\''
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x98)
            {
                InlineVec::Append(out, (uint8_t)'\'');
                i += 2;
                continue;
            }
            // U+2019 right single quote ' (E2 80 99) → '\''
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x99)
            {
                InlineVec::Append(out, (uint8_t)'\'');
                i += 2;
                continue;
            }
        }
        InlineVec::Append(out, c);
    }
    // Swap buffers
    buf.size = 0;
    for (uint32_t i = 0; i < out.size; ++i)
        InlineVec::Append(buf, out.data[i]);
}

} // namespace nyla
