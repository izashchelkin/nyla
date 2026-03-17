#pragma once

#include "nyla/alloc/region_alloc.h"
#include <cstdint>
namespace nyla
{

class GlyphRenderer
{
  public:
    static void Init();

    static void CmdFlush();
};

class CellBuffer
{
  public:
    struct Cell
    {
        uint32_t codepoint;
        uint8_t fg;
        uint8_t bg;

        uint16_t flags;
        static constexpr inline uint16_t kBold = 1 << 0;
        static constexpr inline uint16_t kItalic = 1 << 1;
        static constexpr inline uint16_t kUnderline = 1 << 2;
        static constexpr inline uint16_t kStrike = 1 << 3;
        static constexpr inline uint16_t kInverse = 1 << 4;
        static constexpr inline uint16_t kBlink = 1 << 5;
        static constexpr inline uint16_t kDim = 1 << 6;
    };

  private:
    RegionAlloc *m_Alloc;
};

} // namespace nyla
