#include <array>
#include <cstdint>
#include <span>

#include "nyla/commons/byteparser.h"

namespace nyla
{

struct BdfGlyph
{
    // const char *name;
    std::array<uint8_t, 2> dwidth;
    std::array<uint8_t, 4> bbx;
    std::span<const char> bitmap;
};

class BdfParser : ByteParser
{
  public:
    void Init(const char *data, uint64_t size)
    {
        ByteParser::Init(data, size);
    }

    auto NextGlyph() -> BdfGlyph;
};

} // namespace nyla