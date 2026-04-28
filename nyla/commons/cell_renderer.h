#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct cell_attr
{
    uint16_t glyphIndex;
    uint16_t flags;
    uint32_t fgRgba;
    uint32_t bgRgba;
};

struct cell_renderer_init_desc
{
    uint64_t bdfGuid;
    uint32_t maxCells = 256u * 96u;
    uint32_t cellPxW = 16;
    uint32_t cellPxH = 32;
    uint32_t atlasGlyphsPerRow = 64;
    uint32_t atlasGlyphsPerCol = 32;
};

namespace CellRenderer
{

void API Bootstrap(region_alloc &alloc, const cell_renderer_init_desc &desc);

void API Begin(int32_t originPxX, int32_t originPxY, uint32_t cols, uint32_t rows);

void API PutCell(uint32_t col, uint32_t row, cell_attr cell);

void API Text(uint32_t col, uint32_t row, byteview text, uint32_t fgRgba, uint32_t bgRgba);

auto API GlyphForCodepoint(uint32_t codepoint) -> uint16_t;

void API CmdFlush(rhi_cmdlist cmd);

} // namespace CellRenderer

} // namespace nyla
