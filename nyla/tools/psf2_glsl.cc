#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "nyla/commons/logging/init.h"

namespace nyla
{

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static uint16_t rd16(const uint8_t *p)
{
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

int Main()
{
    LoggingInit();

    std::vector<uint8_t> buf;
    {
        std::array<uint8_t, 1 << 14> tmp{};
        ssize_t r;
        while ((r = ::read(STDIN_FILENO, tmp.data(), tmp.size())) > 0)
            buf.insert(buf.end(), tmp.begin(), tmp.begin() + r);
        if (r < 0)
        {
            perror("read");
            return 1;
        }
    }
    if (buf.size() < 32)
    {
        NYLA_ASSERT(false && "File too small");
    }

    const uint8_t *p = buf.data();
    uint32_t magic = rd32(p + 0);
    uint32_t version = rd32(p + 4);
    uint32_t header_size = rd32(p + 8);
    uint32_t flags = rd32(p + 12);
    uint32_t length = rd32(p + 16);
    uint32_t glyph_size = rd32(p + 20);
    uint32_t height = rd32(p + 24);
    uint32_t width = rd32(p + 28);

    if (magic != 0x864ab572u)
    {
        NYLA_ASSERT(false && "Not PSF2");
    }
    if (header_size < 32 || buf.size() < header_size)
    {
        NYLA_ASSERT(false && "Bad/truncated header");
    }

    const uint8_t *glyphs = p + header_size;
    const uint32_t row_bytes = (width + 7) / 8;
    const uint64_t expect_glyph = uint64_t(height) * row_bytes;
    if (glyph_size != expect_glyph)
    {
        NYLA_LOG("glyph_size mismatch header=%d computed=%d (continuing)", glyph_size, expect_glyph);
    }
    const uint64_t glyph_table_bytes = uint64_t(length) * glyph_size;
    if (buf.data() + buf.size() < glyphs + glyph_table_bytes)
    {
        NYLA_ASSERT(false && "Truncated glyph table");
    }

    std::unordered_map<uint32_t, uint32_t> cp_to_glyph;
    const uint8_t *uni = glyphs + glyph_table_bytes;
    const uint8_t *end = buf.data() + buf.size();
    {
        const uint16_t SEP_END = 0xFFFF;
        const uint16_t SEP_SEQ = 0xFFFE;
        for (uint32_t gi = 0; uni + 2 <= end && gi < length; ++gi)
        {
            while (uni + 2 <= end)
            {
                uint16_t u = rd16(uni);
                uni += 2;
                if (u == SEP_END)
                    break;
                if (u == SEP_SEQ)
                    continue;
                if (!cp_to_glyph.count(u))
                    cp_to_glyph[u] = gi;
            }
        }
    }

    if (row_bytes != 1 || height > 32)
    {
        NYLA_ASSERT(false && "This generator targets up to 8x32 fonts");
        return 2;
    }

    auto glyph_ptr = [&](uint32_t gi) -> const uint8_t * { return glyphs + uint64_t(gi) * glyph_size; };

    auto glyph_for_cp = [&](uint32_t cp) -> uint32_t {
        auto it = cp_to_glyph.find(cp);
        if (it != cp_to_glyph.end())
            return it->second;
        if (cp < length)
            return (uint32_t)cp;
        return UINT32_MAX;
    };

    std::ostringstream out;
    out << "// Auto-generated from PSF2 (" << width << "x" << height << ")\n";
    out << "// Packing convention:\n";
    out << "//   Each glyph uses uvec4 {w0,w1,w2,w3} (16 bytes total).\n";
    out << "//   width<=8 -> one byte per row. Rows are MSB-first per PSF2.\n";
    out << "//   w0 contains rows 0..3 (row0 in MSB byte), w1 rows 4..7, etc.\n";
    out << "//   If height < 32, trailing rows are zero.\n\n";
    out << "const uint WIDTH  = " << width << "u;\n";
    out << "const uint HEIGHT = " << height << "u;\n";
    out << "const uvec4 font_data[96] = {\n";

    for (uint32_t c = 0x20; c <= 0x7F; ++c)
    {
        uint32_t gi = glyph_for_cp(c);
        uint8_t rows[32]{};
        if (gi != UINT32_MAX)
        {
            const uint8_t *g = glyph_ptr(gi);
            for (uint32_t y = 0; y < std::min<uint32_t>(height, 32); ++y)
            {
                rows[y] = g[y];
            }
        }
        else
        {
            NYLA_LOG() << "Note: codepoint U+" << std::hex << std::uppercase << c
                       << " not mapped; emitting blank glyph.\n"
                       << std::dec;
        }

        auto pack4 = [&](uint32_t base_row) -> uint32_t {
            uint32_t w = 0;
            for (int k = 0; k < 4; ++k)
            {
                uint32_t y = base_row + k;
                uint32_t byte = (y < 32) ? rows[y] : 0;
                w |= (byte & 0xFFu) << (8 * (3 - k));
            }
            return w;
        };
        uint32_t w0 = pack4(0);
        uint32_t w1 = pack4(4);
        uint32_t w2 = pack4(8);
        uint32_t w3 = pack4(12);

        auto hex8 = [](uint32_t v) -> std::string {
            std::ostringstream s;
            s << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << v;
            return s.str();
        };
        std::string comment;
        if (c >= 0x20 && c <= 0x7E)
        {
            char ch = static_cast<char>(c);
            if (ch == '\\')
                comment = "'\\\\'";
            else if (ch == '\'')
                comment = "'\\''";
            else
                comment = std::string("'") + ch + "'";
        }
        else if (c == 0x7F)
        {
            comment = "BACKSPACE";
        }
        out << "  uvec4(" << hex8(w0) << ", " << hex8(w1) << ", " << hex8(w2) << ", " << hex8(w3) << ")";
        out << ", // 0x" << std::hex << std::uppercase << c << std::dec << (comment.empty() ? "" : ": " + comment)
            << "\n";
    }
    out << "};\n";

    std::cout << out.str();
    return 0;
}

} // namespace nyla

int main()
{
    return nyla::Main();
}