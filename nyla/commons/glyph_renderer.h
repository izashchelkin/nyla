#pragma once

#include "nyla/commons/span_def.h"
#include <cstdint>

namespace nyla
{

class GlyphRenderer
{
  public:
    static void Init(uint8_t *atlas, uint32_t width, uint32_t height);

    static void CmdFlush();
};

class CellBuffer
{
  public:
    struct cell
    {
        uint32_t codepoint;
        uint8_t fg;
        uint8_t bg;
        uint16_t flags;

        static constexpr inline uint16_t kDefaultBg = 1 << 0;
        static constexpr inline uint16_t kDefaultFg = 1 << 1;

        static constexpr inline uint16_t kBold = 1 << 2;
        static constexpr inline uint16_t kItalic = 1 << 3;
        static constexpr inline uint16_t kUnderline = 1 << 4;
        static constexpr inline uint16_t kStrike = 1 << 5;
        static constexpr inline uint16_t kInverse = 1 << 6;
        static constexpr inline uint16_t kBlink = 1 << 7;
        static constexpr inline uint16_t kDim = 1 << 8;
    };

    void ResizeIfNeeded(uint32_t width, uint32_t height);

  private:
    uint32_t m_CellHeight = 32;
    uint32_t m_CellWidth = 16;

    void *memory;
    span<cell> m_Cells;
    uint32_t m_Height;
    uint32_t m_Width;
    uint32_t m_Head;
    uint32_t m_Flags;

    static constexpr inline uint32_t kDefaultBg = 1 << 0;
    static constexpr inline uint32_t kDefaultFg = 1 << 0;
};

} // namespace nyla