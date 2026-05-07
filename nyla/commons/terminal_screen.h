#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

// Bit flags for terminal_cell::attrs.
namespace TerminalAttr
{
constexpr uint32_t Bold = 1u << 0;
constexpr uint32_t Underline = 1u << 1;
constexpr uint32_t Reverse = 1u << 2;
} // namespace TerminalAttr

struct terminal_cell
{
    uint32_t codepoint;
    uint32_t fgRgba;
    uint32_t bgRgba;
    uint32_t attrs;
};

struct terminal_screen_init_desc
{
    uint32_t cols;
    uint32_t rows;
    uint32_t scrollbackLines; // ring capacity in lines; 0 disables scrollback
    uint32_t defaultFgRgba;
    uint32_t defaultBgRgba;
    span<const uint32_t> palette256; // packed 0xRRGGBB; size 256
};

struct terminal_screen;

namespace TerminalScreen
{

auto API Create(region_alloc &alloc, const terminal_screen_init_desc &desc) -> terminal_screen *;

// Resize cell + scrollback storage to (newCols, newRows). Cells clip-truncate (no reflow);
// scrollback is wiped on cols change since per-line storage is row-major-by-cols.
void API Resize(terminal_screen &self, uint32_t newCols, uint32_t newRows);

// Feed bytes from a pty. Parser is byte-oriented and re-entrant across calls.
void API Feed(terminal_screen &self, byteview bytes);

auto API Cols(const terminal_screen &self) -> uint32_t;
auto API Rows(const terminal_screen &self) -> uint32_t;
auto API CursorCol(const terminal_screen &self) -> uint32_t;
auto API CursorRow(const terminal_screen &self) -> uint32_t;
auto API CursorVisible(const terminal_screen &self) -> bool;
auto API ApplicationCursorKeys(const terminal_screen &self) -> bool;

// Read-only cell access. (col, row) origin top-left.
auto API CellAt(const terminal_screen &self, uint32_t col, uint32_t row) -> terminal_cell;

// Scrollback: lines evicted off the top by ScrollUp. line=0 is oldest stored, line=Count-1 is most-recent.
auto API ScrollbackCount(const terminal_screen &self) -> uint32_t;
auto API ScrollbackCellAt(const terminal_screen &self, uint32_t col, uint32_t line) -> terminal_cell;

} // namespace TerminalScreen

} // namespace nyla
