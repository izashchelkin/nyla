#include <array>
#include <cstdint>

#include "nyla/commons/bdf/bdf.h"

namespace nyla
{

auto BuildFontAtlas(BdfParser &parser, RegionAlloc &alloc) -> Span<uint8_t>;

class ColorTheme
{
  public:
    static void Init();

    static uint32_t bg;
    static uint32_t fg;
    static Array<uint32_t, 256> palette;
};

} // namespace nyla
