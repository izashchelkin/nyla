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
constexpr uint32_t Dim = 1u << 3;
constexpr uint32_t Italic = 1u << 4;
constexpr uint32_t Strike = 1u << 5;
} // namespace TerminalAttr

enum class terminal_cursor_style : uint32_t
{
    BlinkingBlock = 0,
    SteadyBlock = 1,
    BlinkingUnderline = 2,
    SteadyUnderline = 3,
    BlinkingBar = 4,
    SteadyBar = 5,
};

enum class terminal_mouse_mode : uint32_t
{
    None = 0,
    X10 = 1,         // ?1000
    Normal = 2,      // ?1000 but with button releases
    ButtonEvent = 3, // ?1002
    AnyEvent = 4,    // ?1003
};

enum class terminal_mouse_format : uint32_t
{
    Default = 0,
    Utf8 = 1,  // ?1005
    Sgr = 2,   // ?1006
    Urxvt = 3, // ?1015
};

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
auto API CursorStyle(const terminal_screen &self) -> terminal_cursor_style;
auto API ApplicationCursorKeys(const terminal_screen &self) -> bool;
auto API MouseMode(const terminal_screen &self) -> terminal_mouse_mode;
auto API MouseFormat(const terminal_screen &self) -> terminal_mouse_format;

// Read-only cell access. (col, row) origin top-left.
auto API CellAt(const terminal_screen &self, uint32_t col, uint32_t row) -> terminal_cell;

// Retrieves pending reply bytes (e.g. DSR) and clears the internal reply buffer.
auto API PollReply(terminal_screen &self) -> byteview;

// Retrieves the current window title if it has changed since the last call, otherwise returns empty.
auto API PollTitle(terminal_screen &self) -> byteview;

// Scrollback: lines evicted off the top by ScrollUp. line=0 is oldest stored, line=Count-1 is most-recent.
auto API ScrollbackCount(const terminal_screen &self) -> uint32_t;
auto API ScrollbackCellAt(const terminal_screen &self, uint32_t col, uint32_t line) -> terminal_cell;

} // namespace TerminalScreen

} // namespace nyla
