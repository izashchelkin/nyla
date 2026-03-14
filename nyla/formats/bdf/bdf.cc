#include "nyla/formats/bdf/bdf.h"
#include "nyla/commons/assert.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

auto BdfParser::NextGlyph() -> BdfGlyph
{
    BdfGlyph ret;

    for (;;)
    {
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
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("BBXWIDTH "));
        NextLine();

        NYLA_ASSERT(StartsWithAdvance("BITMAP "));
    }
}

} // namespace nyla