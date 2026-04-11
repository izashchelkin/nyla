#include "nyla/commons/bdf.h"

#include <cstdint>

#include "nyla/commons/assert.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/hex.h"

namespace nyla
{

auto NextGlyph(bdf_parser &self, region_alloc &alloc, bdf_glyph &out) -> bool
{
    for (;;)
    {
        if (ByteParser::StartsWith(self, "ENDFONT"_s))
            return false;

        if (!ByteParser::StartsWithAdvance(self, "ENCODING "_s))
        {
            ByteParser::NextLine(self);
            continue;
        }

        out.encoding = CastU32(ByteParser::ParseLong(self));
        ByteParser::NextLine(self);

        NYLA_ASSERT(ByteParser::StartsWithAdvance(self, "SWIDTH "_s));
        ByteParser::NextLine(self);

        NYLA_ASSERT(ByteParser::StartsWithAdvance(self, "DWIDTH "_s));
        for (uint32_t i = 0; i < 2; ++i)
        {
            out.dwidth[i] = CastI32(ByteParser::ParseLong(self));
            if (i != 1)
                NYLA_ASSERT(ByteParser::Read(self) == ' ');
        }
        ByteParser::NextLine(self);

        NYLA_ASSERT(ByteParser::StartsWithAdvance(self, "BBX "_s));
        for (uint32_t i = 0; i < 4; ++i)
        {
            int64_t l = ByteParser::ParseLong(self);
            out.bbx[i] = CastI32(l);
            if (i != 3)
                NYLA_ASSERT(ByteParser::Read(self) == ' ');
        }
        ByteParser::NextLine(self);

        NYLA_ASSERT(ByteParser::StartsWithAdvance(self, "BITMAP"_s));
        ByteParser::NextLine(self);

        span<uint8_t> data = RegionAlloc::AllocArray<uint8_t>(alloc, 32ULL * 2ULL);
        uint32_t i = 0;

        uint32_t count = 0;
        while (!ByteParser::StartsWith(self, "ENDCHAR"_s))
        {
            char ch1 = ByteParser::Read(self);
            char ch2 = ByteParser::Read(self);
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