#include "nyla/apps/terminal/bdf_parser.h"
#include <cstdint>
#include <string_view>

namespace nyla
{

auto BdfParser::FindGlyph(uint16_t encoding) -> BdfGlyph
{
    BdfGlyph ret;

    for (;;)
    {
        const char *at = m_Data.data();
        uint32_t lineLength = 0;
        while (*(at + lineLength) != '\n')
            ++lineLength;

        std::string_view line{at, lineLength};
        constexpr std::string_view kSearchPrefix = "ENCODING ";
        if (line.starts_with(kSearchPrefix))
        {
            line.remove_prefix(kSearchPrefix.size());
        }
    }
}

} // namespace nyla