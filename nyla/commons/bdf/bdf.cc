#include "nyla/commons/bdf/bdf.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/hex.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace nyla
{

auto BdfParser::NextGlyph(RegionAlloc *alloc, BdfGlyph &out) -> bool
{
    for (;;)
    {
        if (StartsWith("ENDFONT"))
            return false;

        if (!StartsWithAdvance("ENCODING "))
        {
            NextLine();
            continue;
        }

        out.encoding = CastU32(ParseLong());
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("SWIDTH "));
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("DWIDTH "));
        for (uint32_t i = 0; i < 2; ++i)
        {
            out.dwidth[i] = CastI32(ParseLong());
            if (i != 1)
                NYLA_ASSERT(Pop() == ' ');
        }
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("BBX "));
        for (uint32_t i = 0; i < 4; ++i)
        {
            int64_t l = ParseLong();
            out.bbx[i] = CastI32(l);
            if (i != 3)
                NYLA_ASSERT(Pop() == ' ');
        }
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("BITMAP"));
        NextLine();

        auto &data = *alloc->Push<Array<uint8_t, 32ULL * 2ULL>>();
        uint32_t i = 0;

        uint32_t count = 0;
        while (!StartsWith("ENDCHAR"))
        {
            char ch1 = Pop();
            char ch2 = Pop();
            data[i++] = ParseHexByte(ch1, ch2);
            ++count;

            ch1 = Pop();
            ch2 = Pop();
            data[i++] = ParseHexByte(ch1, ch2);
            ++count;

            NextLine();
        }

        out.bitmap = Span{data};

        return true;
    }
}

} // namespace nyla