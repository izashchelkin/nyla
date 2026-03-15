#include "nyla/formats/bdf/bdf.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/hex.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

auto BdfParser::NextGlyph(BdfGlyph &out) -> bool
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

        const uint64_t encoding = ParseLong();
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("SWIDTH "));
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("DWIDTH "));
        for (uint32_t i = 0; i < 2; ++i)
            out.dwidth[i] = CastI32(ParseLong());
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("BBX "));
        for (uint32_t i = 0; i < 4; ++i)
            out.bbx[i] = CastI32(ParseLong());
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("BITMAP"));
        NextLine();

        while (!StartsWith("ENDCHAR"))
        {
            char ch1 = Pop();
            char ch2 = Pop();
            ParseHexByte(ch1, ch2);

            ch1 = Pop();
            ch2 = Pop();
            ParseHexByte(ch1, ch2);

            NextLine();
        }

        return true;
    }
}

} // namespace nyla