#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/byteparser.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

namespace
{

constexpr uint32_t kPsf2Magic = 0x864ab572u;
constexpr uint32_t kCodepointBegin = 0x20;
constexpr uint32_t kCodepointEnd = 0x80;
constexpr uint32_t kCodepointCount = kCodepointEnd - kCodepointBegin;
constexpr uint32_t kMaxRows = 32;
constexpr uint16_t kUniSepEnd = 0xFFFF;
constexpr uint16_t kUniSepSeq = 0xFFFE;

auto Pack4Rows(const array<uint8_t, kMaxRows> &rows, uint32_t baseRow) -> uint32_t
{
    uint32_t w = 0;
    for (uint32_t k = 0; k < 4; ++k)
    {
        uint32_t y = baseRow + k;
        uint8_t b = (y < kMaxRows) ? rows[y] : 0;
        w |= ((uint32_t)b) << (8 * (3 - k));
    }
    return w;
}

} // namespace

void UserMain()
{
    auto alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    auto &inputVec = RegionAlloc::AllocVec<uint8_t, 1_MiB>(alloc);
    {
        file_handle in = GetStdin();
        array<uint8_t, 16_KiB> tmp{};
        uint32_t n;
        while ((n = FileRead(in, (uint32_t)Array::Size(tmp), tmp.data)) > 0)
            InlineVec::Append(inputVec, span<const uint8_t>{.data = tmp.data, .size = n});
    }
    byteview buf = inputVec;
    ASSERT(buf.size >= 32, "File too small");

    byte_parser hp;
    ByteParser::Init(hp, buf.data, buf.size);
    uint32_t magic = ByteParser::Read32(hp);
    (void)ByteParser::Read32(hp); // version
    uint32_t headerSize = ByteParser::Read32(hp);
    (void)ByteParser::Read32(hp); // flags
    uint32_t length = ByteParser::Read32(hp);
    uint32_t glyphSize = ByteParser::Read32(hp);
    uint32_t height = ByteParser::Read32(hp);
    uint32_t width = ByteParser::Read32(hp);

    ASSERT(magic == kPsf2Magic, "Not PSF2");
    ASSERT(headerSize >= 32 && buf.size >= headerSize, "Bad/truncated header");

    const uint8_t *glyphs = buf.data + headerSize;
    uint32_t rowBytes = (width + 7) / 8;
    uint64_t expectGlyph = uint64_t(height) * rowBytes;
    if (glyphSize != expectGlyph)
        LOG("glyph_size mismatch header=%u computed=%llu (continuing)", glyphSize, (unsigned long long)expectGlyph);

    uint64_t glyphTableBytes = uint64_t(length) * glyphSize;
    ASSERT(buf.data + buf.size >= glyphs + glyphTableBytes, "Truncated glyph table");
    ASSERT(rowBytes == 1 && height <= kMaxRows, "This generator targets up to 8x32 fonts");

    array<uint32_t, kCodepointCount> cpToGlyph;
    for (uint32_t i = 0; i < kCodepointCount; ++i)
        cpToGlyph[i] = Limits<uint32_t>::Max();

    {
        const uint8_t *uniBegin = glyphs + glyphTableBytes;
        const uint8_t *uniEnd = buf.data + buf.size;
        byte_parser up;
        ByteParser::Init(up, uniBegin, uniEnd - uniBegin);

        for (uint32_t gi = 0; ByteParser::BytesLeft(up) >= 2 && gi < length; ++gi)
        {
            while (ByteParser::BytesLeft(up) >= 2)
            {
                uint16_t u = ByteParser::Read16(up);
                if (u == kUniSepEnd)
                    break;
                if (u == kUniSepSeq)
                    continue;
                if (u >= kCodepointBegin && u < kCodepointEnd)
                {
                    uint32_t idx = u - kCodepointBegin;
                    if (cpToGlyph[idx] == Limits<uint32_t>::Max())
                        cpToGlyph[idx] = gi;
                }
            }
        }
    }

    file_handle out = GetStdout();
    FileWriteFmt(out, "// Auto-generated from PSF2 (%ux%u)\n"_s, width, height);
    FileWriteFmt(out, "// Packing convention:\n"_s);
    FileWriteFmt(out, "//   Each glyph uses uvec4 {w0,w1,w2,w3} (16 bytes total).\n"_s);
    FileWriteFmt(out, "//   width<=8 -> one byte per row. Rows are MSB-first per PSF2.\n"_s);
    FileWriteFmt(out, "//   w0 contains rows 0..3 (row0 in MSB byte), w1 rows 4..7, etc.\n"_s);
    FileWriteFmt(out, "//   If height < 32, trailing rows are zero.\n\n"_s);
    FileWriteFmt(out, "const uint WIDTH  = %uu;\n"_s, width);
    FileWriteFmt(out, "const uint HEIGHT = %uu;\n"_s, height);
    FileWriteFmt(out, "const uvec4 font_data[96] = {\n"_s);

    for (uint32_t c = kCodepointBegin; c < kCodepointEnd; ++c)
    {
        uint32_t gi;
        {
            uint32_t mapped = cpToGlyph[c - kCodepointBegin];
            if (mapped != Limits<uint32_t>::Max())
                gi = mapped;
            else if (c < length)
                gi = c;
            else
                gi = Limits<uint32_t>::Max();
        }

        array<uint8_t, kMaxRows> rows{};
        if (gi != Limits<uint32_t>::Max())
        {
            const uint8_t *g = glyphs + uint64_t(gi) * glyphSize;
            uint32_t copyRows = Min<uint32_t>(height, kMaxRows);
            MemCpy(rows.data, g, copyRows);
        }
        else
        {
            LOG("Note: codepoint U+%X not mapped; emitting blank glyph.", c);
        }

        uint32_t w0 = Pack4Rows(rows, 0);
        uint32_t w1 = Pack4Rows(rows, 4);
        uint32_t w2 = Pack4Rows(rows, 8);
        uint32_t w3 = Pack4Rows(rows, 12);

        if (c >= 0x20 && c <= 0x7E)
        {
            char ch = (char)c;
            if (ch == '\\')
                FileWriteFmt(out, "  uvec4(0x%08X, 0x%08X, 0x%08X, 0x%08X), // 0x%X: '\\\\'\n"_s, w0, w1, w2, w3, c);
            else if (ch == '\'')
                FileWriteFmt(out, "  uvec4(0x%08X, 0x%08X, 0x%08X, 0x%08X), // 0x%X: '\\''\n"_s, w0, w1, w2, w3, c);
            else
                FileWriteFmt(out, "  uvec4(0x%08X, 0x%08X, 0x%08X, 0x%08X), // 0x%X: '%c'\n"_s, w0, w1, w2, w3, c, ch);
        }
        else if (c == 0x7F)
        {
            FileWriteFmt(out, "  uvec4(0x%08X, 0x%08X, 0x%08X, 0x%08X), // 0x%X: BACKSPACE\n"_s, w0, w1, w2, w3, c);
        }
        else
        {
            FileWriteFmt(out, "  uvec4(0x%08X, 0x%08X, 0x%08X, 0x%08X), // 0x%X\n"_s, w0, w1, w2, w3, c);
        }
    }

    FileWriteFmt(out, "};\n"_s);
}

} // namespace nyla
