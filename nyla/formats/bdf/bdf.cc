#include "nyla/formats/bdf/bdf.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/hex.h"

#include <cstdint>
#include <span>
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

        const auto mark = m_Alloc->GetAt();

        uint32_t count = 0;
        while (!StartsWith("ENDCHAR"))
        {
            char ch1 = Pop();
            char ch2 = Pop();
            m_Alloc->Push(ParseHexByte(ch1, ch2));
            ++count;

            ch1 = Pop();
            ch2 = Pop();
            m_Alloc->Push(ParseHexByte(ch1, ch2));
            ++count;

            NextLine();
        }

        out.bitmap = std::span<uint8_t>{(uint8_t *)mark, count};

        return true;
    }
}

} // namespace nyla