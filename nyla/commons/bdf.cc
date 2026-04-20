#include "nyla/commons/bdf.h"

#include <cstdint>

#include "nyla/commons/cast.h"
#include "nyla/commons/hex.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/stringparser.h"

namespace nyla
{

auto NextGlyph(bdf_parser &self, region_alloc &alloc, bdf_glyph &out) -> bool
{
    auto allocMark = alloc.at;

    for (;;)
    {
        RegionAlloc::Reset(alloc, allocMark);

        if (ByteParser::StartsWith(self, "ENDFONT"_s))
            return false;

        if (!ByteParser::StartsWithAdvance(self, "ENCODING "_s))
        {
            ByteParser::NextLine(self);
            continue;
        }

        out.encoding = CastU32(StringParser::ParseLong(self));
        ByteParser::NextLine(self);

        ASSERT(ByteParser::StartsWithAdvance(self, "SWIDTH "_s));
        ByteParser::NextLine(self);

        ASSERT(ByteParser::StartsWithAdvance(self, "DWIDTH "_s));
        for (uint32_t i = 0; i < 2; ++i)
        {
            out.dwidth[i] = CastI32(StringParser::ParseLong(self));
            if (i != 1)
                ASSERT(ByteParser::Read(self) == ' ');
        }
        ByteParser::NextLine(self);

        ASSERT(ByteParser::StartsWithAdvance(self, "BBX "_s));
        for (uint32_t i = 0; i < 4; ++i)
        {
            int64_t l = StringParser::ParseLong(self);
            out.bbx[i] = CastI32(l);
            if (i != 3)
                ASSERT(ByteParser::Read(self) == ' ');
        }
        ByteParser::NextLine(self);

        ASSERT(ByteParser::StartsWithAdvance(self, "BITMAP"_s));
        ByteParser::NextLine(self);

        span<uint8_t> data = RegionAlloc::AllocArray<uint8_t>(alloc, 32ULL * 2ULL);
        uint32_t i = 0;

        uint32_t count = 0;
        while (!ByteParser::StartsWith(self, "ENDCHAR"_s))
        {
            uint8_t ch1 = ByteParser::Read(self);
            uint8_t ch2 = ByteParser::Read(self);
            data[i++] = ParseHexByte(ch1, ch2);
            ++count;

            ch1 = ByteParser::Read(self);
            ch2 = ByteParser::Read(self);
            data[i++] = ParseHexByte(ch1, ch2);
            ++count;

            ByteParser::NextLine(self);
        }

        out.bitmap = data;

        return true;
    }
}

} // namespace nyla