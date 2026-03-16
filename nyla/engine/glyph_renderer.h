#pragma once

#include <cstdint>
namespace nyla
{

class GlyphRenderer
{
  public:
    static void Init();

    static void CmdFlush();
};

struct CellBuffer
{
    struct Cell
    {
        uint32_t encoding;
        uint64_t fg;
        uint32_t bg;
    };

    Cell rows;
};

} // namespace nyla
