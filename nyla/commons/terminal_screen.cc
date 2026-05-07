#include "nyla/commons/terminal_screen.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

namespace
{

enum class parser_state : uint8_t
{
    Ground,
    Esc,
    Csi,
    EscCharset, // swallow one byte after ESC ( / ) / * / +
    Osc,        // ESC ] ... BEL | ST
    OscEsc,     // saw ESC inside Osc, expect '\\'
    Dcs,        // ESC P ... ST (swallowed; e.g. DECRQSS reply)
    DcsEsc,     // saw ESC inside Dcs, expect '\\'
};

constexpr uint32_t kMaxParams = 16;

} // namespace

struct terminal_screen
{
    uint32_t cols;
    uint32_t rows;
    uint32_t cursorCol;
    uint32_t cursorRow;

    uint32_t curFg;
    uint32_t curBg;
    uint32_t curAttrs;
    uint32_t defaultFg;
    uint32_t defaultBg;

    span<terminal_cell> cells;     // active viewport (points at primary or alt)
    span<terminal_cell> primary;   // primary cell buffer (cols*rows)
    span<terminal_cell> alt;       // alt screen buffer (cols*rows); used by DECSET ?1049
    // Per-row "this row's logical content continues onto next row" bit. Set by autowrap in
    // PutCodepoint, ridden along by scroll/insert/delete row ops, cleared by row erases.
    // activeWrapped points at primaryWrapped or altWrapped per inAltScreen.
    span<uint8_t> primaryWrapped;
    span<uint8_t> altWrapped;
    span<uint8_t> activeWrapped;
    bool inAltScreen;
    bool applicationCursorKeys;    // DECCKM (?1)
    bool cursorVisible;

    // DECSTBM scrolling region (inclusive, 0-based). Default = full screen.
    uint32_t scrollTop;
    uint32_t scrollBot;
    span<const uint32_t> palette256;

    // Saved cursor (DECSC/DECRC). One slot per screen (primary[0], alt[1]) so DECSC inside
    // the alt screen doesn't trample what ?1049 stashed for the primary on enter.
    uint32_t savedCursorCol[2];
    uint32_t savedCursorRow[2];
    uint32_t savedFg[2];
    uint32_t savedBg[2];
    uint32_t savedAttrs[2];

    // Single own-chunk region for primary + alt + scrollback ring; lets Resize re-alloc cheaply.
    region_alloc screenAlloc;

    // Scrollback ring of full lines (cols cells per line). cap=0 disables.
    span<terminal_cell> scrollback;
    span<uint8_t> scrollbackWrapped;  // parallel ring of wrapped bits, one per scrollback line
    uint32_t scrollbackCap;
    uint32_t scrollbackHead;  // index of next line slot to write
    uint32_t scrollbackCount; // stored line count (<= scrollbackCap)

    parser_state state;
    uint32_t params[kMaxParams];
    uint32_t paramCount;
    bool paramHasDigits;
    bool csiPrivate;
    bool csiIntermediate; // saw any 0x20-0x2F byte in CSI params (DECSCUSR ' ', DECRQM '$' etc)

    // UTF-8 decoder state.
    uint32_t utf8Cp;
    uint8_t utf8Need;
};

namespace TerminalScreen
{

namespace
{

INLINE auto Idx(const terminal_screen &s, uint32_t col, uint32_t row) -> uint32_t
{
    return row * s.cols + col;
}

void EraseRange(terminal_screen &s, uint32_t fromIdx, uint32_t toExclusive)
{
    terminal_cell blank{0x20, s.curFg, s.curBg, s.curAttrs};
    for (uint32_t i = fromIdx; i < toExclusive; ++i)
        s.cells[i] = blank;
}

void PushScrollback(terminal_screen &s, uint32_t srcRow)
{
    if (s.scrollbackCap == 0)
        return;
    if (s.inAltScreen)
        return; // alt screen never feeds scrollback
    terminal_cell *dst = s.scrollback.data + (uint64_t)s.scrollbackHead * s.cols;
    const terminal_cell *src = s.cells.data + (uint64_t)srcRow * s.cols;
    MemCpy(dst, src, sizeof(terminal_cell) * s.cols);
    s.scrollbackWrapped[s.scrollbackHead] = s.activeWrapped[srcRow];
    s.scrollbackHead = (s.scrollbackHead + 1) % s.scrollbackCap;
    if (s.scrollbackCount < s.scrollbackCap)
        ++s.scrollbackCount;
}

// Scroll up by 1 within the current DECSTBM region. Top row of region pushed to
// scrollback only when region starts at row 0 (canonical terminal behavior).
void ScrollUp(terminal_screen &s)
{
    uint32_t top = s.scrollTop;
    uint32_t bot = s.scrollBot;
    if (top == 0)
        PushScrollback(s, top);
    if (bot > top)
    {
        MemMove(s.cells.data + (uint64_t)top * s.cols, s.cells.data + (uint64_t)(top + 1) * s.cols,
                sizeof(terminal_cell) * (uint64_t)(bot - top) * s.cols);
        for (uint32_t r = top; r < bot; ++r)
            s.activeWrapped[r] = s.activeWrapped[r + 1];
    }
    s.activeWrapped[bot] = 0;
    EraseRange(s, Idx(s, 0, bot), Idx(s, 0, bot) + s.cols);
}

// Scroll down by 1 within the current DECSTBM region. No scrollback push (top of region is fed
// by content that previously came from scrollback or above; we don't reverse that).
void ScrollDown(terminal_screen &s)
{
    uint32_t top = s.scrollTop;
    uint32_t bot = s.scrollBot;
    if (bot > top)
    {
        MemMove(s.cells.data + (uint64_t)(top + 1) * s.cols, s.cells.data + (uint64_t)top * s.cols,
                sizeof(terminal_cell) * (uint64_t)(bot - top) * s.cols);
        for (int32_t r = (int32_t)bot; r > (int32_t)top; --r)
            s.activeWrapped[r] = s.activeWrapped[r - 1];
    }
    s.activeWrapped[top] = 0;
    EraseRange(s, Idx(s, 0, top), Idx(s, 0, top) + s.cols);
}

void Newline(terminal_screen &s)
{
    if (s.cursorRow == s.scrollBot)
        ScrollUp(s);
    else if (s.cursorRow + 1 < s.rows)
        ++s.cursorRow;
}

INLINE auto FromIdx256(span<const uint32_t> palette, uint32_t idx) -> uint32_t
{
    if (idx >= palette.size)
        idx = 7;
    return 0xFF000000u | (palette[idx] & 0x00FFFFFFu);
}

INLINE auto FromRgb(uint32_t r, uint32_t g, uint32_t b) -> uint32_t
{
    return 0xFF000000u | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

void PutCodepoint(terminal_screen &s, uint32_t cp)
{
    if (s.cursorCol >= s.cols)
    {
        s.activeWrapped[s.cursorRow] = 1;
        s.cursorCol = 0;
        Newline(s);
    }
    s.cells[Idx(s, s.cursorCol, s.cursorRow)] = terminal_cell{cp, s.curFg, s.curBg, s.curAttrs};
    ++s.cursorCol;
}

void PushParam(terminal_screen &s, uint32_t v)
{
    if (s.paramCount < kMaxParams)
        s.params[s.paramCount++] = v;
    s.paramHasDigits = false;
}

INLINE auto GetParam(const terminal_screen &s, uint32_t i, uint32_t dflt) -> uint32_t
{
    if (i >= s.paramCount)
        return dflt;
    if (s.params[i] == 0xFFFFFFFFu)
        return dflt;
    return s.params[i];
}

void ApplySgr(terminal_screen &s)
{
    uint32_t i = 0;
    if (s.paramCount == 0)
    {
        s.curFg = s.defaultFg;
        s.curBg = s.defaultBg;
        s.curAttrs = 0;
        return;
    }
    while (i < s.paramCount)
    {
        uint32_t p = GetParam(s, i, 0);
        if (p == 0)
        {
            s.curFg = s.defaultFg;
            s.curBg = s.defaultBg;
            s.curAttrs = 0;
            ++i;
        }
        else if (p == 1)
        {
            s.curAttrs |= TerminalAttr::Bold;
            ++i;
        }
        else if (p == 4)
        {
            s.curAttrs |= TerminalAttr::Underline;
            ++i;
        }
        else if (p == 7)
        {
            s.curAttrs |= TerminalAttr::Reverse;
            ++i;
        }
        else if (p == 22)
        {
            s.curAttrs &= ~TerminalAttr::Bold;
            ++i;
        }
        else if (p == 24)
        {
            s.curAttrs &= ~TerminalAttr::Underline;
            ++i;
        }
        else if (p == 27)
        {
            s.curAttrs &= ~TerminalAttr::Reverse;
            ++i;
        }
        else if (p >= 30 && p <= 37)
        {
            s.curFg = FromIdx256(s.palette256, p - 30);
            ++i;
        }
        else if (p == 38)
        {
            uint32_t mode = GetParam(s, i + 1, 0);
            if (mode == 5)
            {
                s.curFg = FromIdx256(s.palette256, GetParam(s, i + 2, 0));
                i += 3;
            }
            else if (mode == 2)
            {
                s.curFg = FromRgb(GetParam(s, i + 2, 0), GetParam(s, i + 3, 0), GetParam(s, i + 4, 0));
                i += 5;
            }
            else
                ++i;
        }
        else if (p == 39)
        {
            s.curFg = s.defaultFg;
            ++i;
        }
        else if (p >= 40 && p <= 47)
        {
            s.curBg = FromIdx256(s.palette256, p - 40);
            ++i;
        }
        else if (p == 48)
        {
            uint32_t mode = GetParam(s, i + 1, 0);
            if (mode == 5)
            {
                s.curBg = FromIdx256(s.palette256, GetParam(s, i + 2, 0));
                i += 3;
            }
            else if (mode == 2)
            {
                s.curBg = FromRgb(GetParam(s, i + 2, 0), GetParam(s, i + 3, 0), GetParam(s, i + 4, 0));
                i += 5;
            }
            else
                ++i;
        }
        else if (p == 49)
        {
            s.curBg = s.defaultBg;
            ++i;
        }
        else if (p >= 90 && p <= 97)
        {
            s.curFg = FromIdx256(s.palette256, 8 + (p - 90));
            ++i;
        }
        else if (p >= 100 && p <= 107)
        {
            s.curBg = FromIdx256(s.palette256, 8 + (p - 100));
            ++i;
        }
        else
            ++i;
    }
}

void EnterAltScreen(terminal_screen &s)
{
    if (s.inAltScreen)
        return;
    // ?1049 enter saves cursor + SGR via the primary slot; alt slot starts fresh.
    s.savedCursorCol[0] = s.cursorCol;
    s.savedCursorRow[0] = s.cursorRow;
    s.savedFg[0] = s.curFg;
    s.savedBg[0] = s.curBg;
    s.savedAttrs[0] = s.curAttrs;
    s.savedCursorCol[1] = 0;
    s.savedCursorRow[1] = 0;
    s.savedFg[1] = s.defaultFg;
    s.savedBg[1] = s.defaultBg;
    s.savedAttrs[1] = 0;
    s.inAltScreen = true;
    s.cells = s.alt;
    s.activeWrapped = s.altWrapped;
    for (uint32_t r = 0; r < s.rows; ++r)
        s.activeWrapped[r] = 0;
    EraseRange(s, 0, s.cols * s.rows);
    s.cursorCol = 0;
    s.cursorRow = 0;
}

void LeaveAltScreen(terminal_screen &s)
{
    if (!s.inAltScreen)
        return;
    s.inAltScreen = false;
    s.cells = s.primary;
    s.activeWrapped = s.primaryWrapped;
    s.cursorCol = s.savedCursorCol[0];
    s.cursorRow = s.savedCursorRow[0];
    s.curFg = s.savedFg[0];
    s.curBg = s.savedBg[0];
    s.curAttrs = s.savedAttrs[0];
}

// Insert n blank cells at cursor on current line; cells past col=cols-1 fall off.
void InsertChars(terminal_screen &s, uint32_t n)
{
    if (n == 0)
        return;
    if (n > s.cols - s.cursorCol)
        n = s.cols - s.cursorCol;
    uint32_t rowStart = Idx(s, 0, s.cursorRow);
    for (int32_t c = (int32_t)s.cols - 1; c >= (int32_t)(s.cursorCol + n); --c)
        s.cells[rowStart + c] = s.cells[rowStart + c - n];
    terminal_cell blank{0x20, s.curFg, s.curBg, s.curAttrs};
    for (uint32_t c = s.cursorCol; c < s.cursorCol + n; ++c)
        s.cells[rowStart + c] = blank;
}

// Delete n cells at cursor on current line; tail shifts left, cells fill from right with blanks.
void DeleteChars(terminal_screen &s, uint32_t n)
{
    if (n == 0)
        return;
    if (n > s.cols - s.cursorCol)
        n = s.cols - s.cursorCol;
    uint32_t rowStart = Idx(s, 0, s.cursorRow);
    for (uint32_t c = s.cursorCol; c + n < s.cols; ++c)
        s.cells[rowStart + c] = s.cells[rowStart + c + n];
    terminal_cell blank{0x20, s.curFg, s.curBg, s.curAttrs};
    for (uint32_t c = s.cols - n; c < s.cols; ++c)
        s.cells[rowStart + c] = blank;
}

// Insert n blank lines at cursor row within DECSTBM region; lines below shift down within region;
// rows past scrollBot fall off. No-op if cursor is outside the scrolling region.
void InsertLines(terminal_screen &s, uint32_t n)
{
    if (n == 0)
        return;
    if (s.cursorRow < s.scrollTop || s.cursorRow > s.scrollBot)
        return;
    uint32_t regionRows = s.scrollBot - s.cursorRow + 1;
    if (n > regionRows)
        n = regionRows;
    uint32_t shift = regionRows - n;
    if (shift > 0)
    {
        MemMove(s.cells.data + (uint64_t)(s.cursorRow + n) * s.cols,
                s.cells.data + (uint64_t)s.cursorRow * s.cols,
                sizeof(terminal_cell) * (uint64_t)shift * s.cols);
        for (int32_t r = (int32_t)s.scrollBot; r >= (int32_t)(s.cursorRow + n); --r)
            s.activeWrapped[r] = s.activeWrapped[r - n];
    }
    for (uint32_t r = s.cursorRow; r < s.cursorRow + n; ++r)
        s.activeWrapped[r] = 0;
    EraseRange(s, Idx(s, 0, s.cursorRow), Idx(s, 0, s.cursorRow) + n * s.cols);
}

// Delete n lines from cursor row within DECSTBM region; lines below shift up within region; bottom of region blanked.
void DeleteLines(terminal_screen &s, uint32_t n)
{
    if (n == 0)
        return;
    if (s.cursorRow < s.scrollTop || s.cursorRow > s.scrollBot)
        return;
    uint32_t regionRows = s.scrollBot - s.cursorRow + 1;
    if (n > regionRows)
        n = regionRows;
    uint32_t shift = regionRows - n;
    if (shift > 0)
    {
        MemMove(s.cells.data + (uint64_t)s.cursorRow * s.cols,
                s.cells.data + (uint64_t)(s.cursorRow + n) * s.cols,
                sizeof(terminal_cell) * (uint64_t)shift * s.cols);
        for (uint32_t r = s.cursorRow; r + n <= s.scrollBot; ++r)
            s.activeWrapped[r] = s.activeWrapped[r + n];
    }
    for (uint32_t r = s.scrollBot + 1 - n; r <= s.scrollBot; ++r)
        s.activeWrapped[r] = 0;
    EraseRange(s, Idx(s, 0, s.scrollBot + 1 - n), Idx(s, 0, s.scrollBot + 1 - n) + n * s.cols);
}

// Erase n cells at/after cursor on current line; cursor unchanged. ECH.
void EraseChars(terminal_screen &s, uint32_t n)
{
    if (n == 0)
        return;
    if (n > s.cols - s.cursorCol)
        n = s.cols - s.cursorCol;
    uint32_t rowStart = Idx(s, 0, s.cursorRow);
    terminal_cell blank{0x20, s.curFg, s.curBg, s.curAttrs};
    for (uint32_t c = s.cursorCol; c < s.cursorCol + n; ++c)
        s.cells[rowStart + c] = blank;
}

void DispatchPrivateMode(terminal_screen &s, bool set)
{
    for (uint32_t i = 0; i < s.paramCount; ++i)
    {
        uint32_t p = GetParam(s, i, 0);
        switch (p)
        {
        case 1:
            s.applicationCursorKeys = set;
            break;
        case 25:
            s.cursorVisible = set;
            break;
        case 1047:
        case 1049:
            if (set)
                EnterAltScreen(s);
            else
                LeaveAltScreen(s);
            break;
        // Modes we deliberately swallow without logging (parsed in the wild but not yet meaningful here).
        case 7:    // DECAWM autowrap (we autowrap regardless)
        case 12:   // cursor blink
        case 1000: // mouse X10
        case 1002: // mouse cell motion
        case 1003: // mouse all motion
        case 1004: // focus events
        case 1005: // mouse utf8
        case 1006: // mouse SGR
        case 2004: // bracketed paste
            break;
        default:
            LOG("terminal_screen: unhandled DECSET/DECRST mode ?%d (set=%d)"_s, p, (uint32_t)set);
            break;
        }
    }
}

void DispatchCsi(terminal_screen &s, uint8_t final)
{
    switch (final)
    {
    case 'A': {
        uint32_t n = GetParam(s, 0, 1);
        s.cursorRow = (n >= s.cursorRow) ? 0 : s.cursorRow - n;
        break;
    }
    case 'B': {
        uint32_t n = GetParam(s, 0, 1);
        s.cursorRow += n;
        if (s.cursorRow >= s.rows)
            s.cursorRow = s.rows - 1;
        break;
    }
    case 'C': {
        uint32_t n = GetParam(s, 0, 1);
        s.cursorCol += n;
        if (s.cursorCol >= s.cols)
            s.cursorCol = s.cols - 1;
        break;
    }
    case 'D': {
        uint32_t n = GetParam(s, 0, 1);
        s.cursorCol = (n >= s.cursorCol) ? 0 : s.cursorCol - n;
        break;
    }
    case 'H':
    case 'f': {
        uint32_t row = GetParam(s, 0, 1);
        uint32_t col = GetParam(s, 1, 1);
        s.cursorRow = (row > 0) ? row - 1 : 0;
        s.cursorCol = (col > 0) ? col - 1 : 0;
        if (s.cursorRow >= s.rows)
            s.cursorRow = s.rows - 1;
        if (s.cursorCol >= s.cols)
            s.cursorCol = s.cols - 1;
        break;
    }
    case 'J': {
        uint32_t n = GetParam(s, 0, 0);
        if (n == 0)
        {
            EraseRange(s, Idx(s, s.cursorCol, s.cursorRow), s.cols * s.rows);
            for (uint32_t r = s.cursorRow; r < s.rows; ++r)
                s.activeWrapped[r] = 0;
        }
        else if (n == 1)
        {
            EraseRange(s, 0, Idx(s, s.cursorCol, s.cursorRow) + 1);
            for (uint32_t r = 0; r <= s.cursorRow && r < s.rows; ++r)
                s.activeWrapped[r] = 0;
        }
        else
        {
            EraseRange(s, 0, s.cols * s.rows);
            for (uint32_t r = 0; r < s.rows; ++r)
                s.activeWrapped[r] = 0;
        }
        break;
    }
    case 'K': {
        uint32_t n = GetParam(s, 0, 0);
        uint32_t lineStart = Idx(s, 0, s.cursorRow);
        if (n == 0)
            EraseRange(s, Idx(s, s.cursorCol, s.cursorRow), lineStart + s.cols);
        else if (n == 1)
            EraseRange(s, lineStart, Idx(s, s.cursorCol, s.cursorRow) + 1);
        else
            EraseRange(s, lineStart, lineStart + s.cols);
        s.activeWrapped[s.cursorRow] = 0;
        break;
    }
    case '@':
        InsertChars(s, GetParam(s, 0, 1));
        break;
    case 'P':
        DeleteChars(s, GetParam(s, 0, 1));
        break;
    case 'L':
        InsertLines(s, GetParam(s, 0, 1));
        break;
    case 'M':
        DeleteLines(s, GetParam(s, 0, 1));
        break;
    case 'X':
        EraseChars(s, GetParam(s, 0, 1));
        break;
    case 'G': {
        uint32_t col = GetParam(s, 0, 1);
        s.cursorCol = (col > 0) ? col - 1 : 0;
        if (s.cursorCol >= s.cols)
            s.cursorCol = s.cols - 1;
        break;
    }
    case 'd': {
        uint32_t row = GetParam(s, 0, 1);
        s.cursorRow = (row > 0) ? row - 1 : 0;
        if (s.cursorRow >= s.rows)
            s.cursorRow = s.rows - 1;
        break;
    }
    case 'S': {
        uint32_t n = GetParam(s, 0, 1);
        for (uint32_t i = 0; i < n; ++i)
            ScrollUp(s);
        break;
    }
    case 'T': {
        // Scroll Down within DECSTBM region. No scrollback push.
        uint32_t n = GetParam(s, 0, 1);
        for (uint32_t k = 0; k < n; ++k)
            ScrollDown(s);
        break;
    }
    case 'm':
        ApplySgr(s);
        break;
    case 'r': {
        // DECSTBM: top;bot (1-based, inclusive). Empty = full screen.
        uint32_t top = GetParam(s, 0, 1);
        uint32_t bot = GetParam(s, 1, s.rows);
        if (top < 1)
            top = 1;
        if (bot > s.rows)
            bot = s.rows;
        if (top >= bot)
        {
            s.scrollTop = 0;
            s.scrollBot = s.rows - 1;
        }
        else
        {
            s.scrollTop = top - 1;
            s.scrollBot = bot - 1;
        }
        // DECSTBM moves cursor to home of region.
        s.cursorCol = 0;
        s.cursorRow = s.scrollTop;
        break;
    }
    case 's': {
        uint32_t slot = s.inAltScreen ? 1 : 0;
        s.savedCursorCol[slot] = s.cursorCol;
        s.savedCursorRow[slot] = s.cursorRow;
        s.savedFg[slot] = s.curFg;
        s.savedBg[slot] = s.curBg;
        s.savedAttrs[slot] = s.curAttrs;
        break;
    }
    case 'u': {
        uint32_t slot = s.inAltScreen ? 1 : 0;
        s.cursorCol = s.savedCursorCol[slot];
        s.cursorRow = s.savedCursorRow[slot];
        s.curFg = s.savedFg[slot];
        s.curBg = s.savedBg[slot];
        s.curAttrs = s.savedAttrs[slot];
        if (s.cursorRow >= s.rows)
            s.cursorRow = s.rows - 1;
        if (s.cursorCol >= s.cols)
            s.cursorCol = s.cols - 1;
        break;
    }
    // Quietly accept finals we don't act on but that real apps emit constantly.
    // 'c' DA, 'n' DSR (replies skipped — apps fall back), 't' XTWINOPS, 'q' DECSCUSR
    // (with ' ' intermediate; bare 'q' here has no defined effect).
    case 'c':
    case 'n':
    case 't':
    case 'q':
        break;
    default:
        // Intermediate byte (0x20-0x2F) seen → CSI ' ' q, CSI '$' p etc; quietly swallow.
        if (s.csiIntermediate)
            break;
        LOG("terminal_screen: unhandled CSI final '%c' (%d)"_s, (char)final, (uint32_t)final);
        break;
    }
}

void ResetCsi(terminal_screen &s)
{
    s.paramCount = 0;
    s.paramHasDigits = false;
    s.csiPrivate = false;
    s.csiIntermediate = false;
    for (uint32_t i = 0; i < kMaxParams; ++i)
        s.params[i] = 0xFFFFFFFFu;
}

void HandleControl(terminal_screen &s, uint8_t b)
{
    switch (b)
    {
    case 0x07:
        break;
    case 0x08:
        if (s.cursorCol > 0)
            --s.cursorCol;
        break;
    case 0x09: {
        uint32_t next = (s.cursorCol + 8) & ~7u;
        if (next >= s.cols)
            next = s.cols - 1;
        s.cursorCol = next;
        break;
    }
    case 0x0A:
    case 0x0B:
    case 0x0C:
        Newline(s);
        break;
    case 0x0D:
        s.cursorCol = 0;
        break;
    default:
        break;
    }
}

void StepGround(terminal_screen &s, uint8_t b)
{
    if (b == 0x1B)
    {
        s.state = parser_state::Esc;
        return;
    }
    if (b < 0x20 || b == 0x7F)
    {
        HandleControl(s, b);
        s.utf8Need = 0;
        return;
    }

    if (s.utf8Need > 0)
    {
        if ((b & 0xC0) != 0x80)
        {
            s.utf8Need = 0;
            PutCodepoint(s, '?');
            // Fallthrough to re-process this byte.
        }
        else
        {
            s.utf8Cp = (s.utf8Cp << 6) | (b & 0x3Fu);
            --s.utf8Need;
            if (s.utf8Need == 0)
                PutCodepoint(s, s.utf8Cp);
            return;
        }
    }

    if (b < 0x80)
    {
        PutCodepoint(s, b);
    }
    else if ((b & 0xE0) == 0xC0)
    {
        s.utf8Cp = b & 0x1Fu;
        s.utf8Need = 1;
    }
    else if ((b & 0xF0) == 0xE0)
    {
        s.utf8Cp = b & 0x0Fu;
        s.utf8Need = 2;
    }
    else if ((b & 0xF8) == 0xF0)
    {
        s.utf8Cp = b & 0x07u;
        s.utf8Need = 3;
    }
    else
    {
        PutCodepoint(s, '?');
    }
}

// Allocate (or re-allocate after Reset) primary, alt, and scrollback storage from screenAlloc.
// Caller is responsible for resetting screenAlloc beforehand on resize. Cells are blanked
// using s.curFg/curBg/curAttrs. Wrapped bit arrays are zeroed.
void AllocBuffers(terminal_screen &s)
{
    uint32_t total = s.cols * s.rows;
    auto *primaryMem =
        (terminal_cell *)RegionAlloc::Alloc(s.screenAlloc, sizeof(terminal_cell) * total, alignof(terminal_cell));
    auto *altMem =
        (terminal_cell *)RegionAlloc::Alloc(s.screenAlloc, sizeof(terminal_cell) * total, alignof(terminal_cell));
    s.primary = span<terminal_cell>{primaryMem, total};
    s.alt = span<terminal_cell>{altMem, total};

    terminal_cell blank{0x20, s.curFg, s.curBg, s.curAttrs};
    for (uint32_t i = 0; i < total; ++i)
    {
        s.primary[i] = blank;
        s.alt[i] = blank;
    }

    s.primaryWrapped = RegionAlloc::AllocArray<uint8_t>(s.screenAlloc, s.rows);
    s.altWrapped = RegionAlloc::AllocArray<uint8_t>(s.screenAlloc, s.rows);
    for (uint32_t r = 0; r < s.rows; ++r)
    {
        s.primaryWrapped[r] = 0;
        s.altWrapped[r] = 0;
    }

    if (s.scrollbackCap > 0)
    {
        s.scrollback = RegionAlloc::AllocArray<terminal_cell>(s.screenAlloc,
                                                              (uint64_t)s.scrollbackCap * s.cols);
        s.scrollbackWrapped = RegionAlloc::AllocArray<uint8_t>(s.screenAlloc, s.scrollbackCap);
        for (uint32_t k = 0; k < s.scrollbackCap; ++k)
            s.scrollbackWrapped[k] = 0;
    }
    else
    {
        s.scrollback = span<terminal_cell>{};
        s.scrollbackWrapped = span<uint8_t>{};
    }
}

} // namespace

auto API Create(region_alloc &alloc, const terminal_screen_init_desc &desc) -> terminal_screen *
{
    auto &self = RegionAlloc::Alloc<terminal_screen>(alloc);
    self.cols = desc.cols;
    self.rows = desc.rows;
    self.cursorCol = 0;
    self.cursorRow = 0;
    self.curFg = desc.defaultFgRgba;
    self.curBg = desc.defaultBgRgba;
    self.curAttrs = 0;
    self.defaultFg = desc.defaultFgRgba;
    self.defaultBg = desc.defaultBgRgba;
    self.palette256 = desc.palette256;

    self.scrollbackCap = desc.scrollbackLines;
    self.scrollbackHead = 0;
    self.scrollbackCount = 0;

    // Single own-chunk region holds primary + alt + scrollback so Resize can rebuild them in place.
    self.screenAlloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    AllocBuffers(self);

    self.cells = self.primary;
    self.activeWrapped = self.primaryWrapped;
    self.inAltScreen = false;
    self.applicationCursorKeys = false;
    self.cursorVisible = true;
    for (uint32_t k = 0; k < 2; ++k)
    {
        self.savedCursorCol[k] = 0;
        self.savedCursorRow[k] = 0;
        self.savedFg[k] = self.curFg;
        self.savedBg[k] = self.curBg;
        self.savedAttrs[k] = 0;
    }
    self.scrollTop = 0;
    self.scrollBot = self.rows - 1;

    self.state = parser_state::Ground;
    ResetCsi(self);
    self.utf8Cp = 0;
    self.utf8Need = 0;
    return &self;
}

// Reflow primary + scrollback from (oldCols, oldRows) into (newCols, newRows).
// Logical lines = consecutive rows joined by the wrapped bit; we unwrap them, then
// re-wrap into newCols-wide rows. Trailing default-blank cells on non-wrapped logical
// lines are trimmed before re-wrap (so a 60-col line in an 80-col grid doesn't pad onto
// a second 40-col row when shrinking). Cursor follows its logical-column position.
// Alt screen is wiped (apps redraw on SIGWINCH).
void API Resize(terminal_screen &self, uint32_t newCols, uint32_t newRows)
{
    if (newCols == 0)
        newCols = 1;
    if (newRows == 0)
        newRows = 1;
    if (newCols == self.cols && newRows == self.rows)
        return;

    uint32_t oldCols = self.cols;
    uint32_t oldRows = self.rows;
    uint32_t srcTotal = self.scrollbackCount + oldRows;
    bool cursorOnPrimary = !self.inAltScreen;
    uint32_t cursorSrcRow = self.scrollbackCount + self.cursorRow;

    auto SrcCells = [&self, oldCols](uint32_t idx) -> const terminal_cell *
    {
        if (idx < self.scrollbackCount)
        {
            uint32_t oldest = (self.scrollbackHead + self.scrollbackCap - self.scrollbackCount) % self.scrollbackCap;
            uint32_t slot = (oldest + idx) % self.scrollbackCap;
            return self.scrollback.data + (uint64_t)slot * oldCols;
        }
        uint32_t row = idx - self.scrollbackCount;
        // Reflow always sources from primary; alt is treated as transient.
        return self.primary.data + (uint64_t)row * oldCols;
    };
    auto SrcWrap = [&self](uint32_t idx) -> uint8_t
    {
        if (idx < self.scrollbackCount)
        {
            uint32_t oldest = (self.scrollbackHead + self.scrollbackCap - self.scrollbackCount) % self.scrollbackCap;
            uint32_t slot = (oldest + idx) % self.scrollbackCap;
            return self.scrollbackWrapped[slot];
        }
        return self.primaryWrapped[idx - self.scrollbackCount];
    };

    terminal_cell defaultBlank{0x20, self.defaultFg, self.defaultBg, 0};
    auto IsBlankDefault = [&self](const terminal_cell &c) -> bool
    {
        return c.codepoint == 0x20 && c.fgRgba == self.defaultFg && c.bgRgba == self.defaultBg && c.attrs == 0;
    };

    // Worst-case emit upper bound: each src row could expand by ceil(oldCols/newCols).
    uint64_t maxEmitted = ((uint64_t)oldCols + newCols - 1) / newCols * (uint64_t)srcTotal + 4;

    region_alloc temp = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    span<terminal_cell> emitCells = RegionAlloc::AllocArray<terminal_cell>(temp, maxEmitted * newCols);
    span<uint8_t> emitWrap = RegionAlloc::AllocArray<uint8_t>(temp, maxEmitted);

    uint32_t emitCount = 0;
    uint32_t cursorEmitRow = 0xFFFFFFFFu;
    uint32_t cursorEmitCol = 0;

    uint32_t i = 0;
    while (i < srcTotal)
    {
        uint32_t start = i;
        // Walk continuation rows: a logical line ends at the first row whose wrapped bit is 0
        // (or at the end of the source stream).
        while (i < srcTotal && SrcWrap(i) && i + 1 < srcTotal)
            ++i;
        uint32_t end = i;
        ++i;

        uint32_t srcCount = end - start + 1;
        bool tailWrapped = SrcWrap(end) != 0;

        // Logical content length: full oldCols for each wrapped intermediate row + trimmed last row.
        uint32_t length = (srcCount - 1) * oldCols;
        if (tailWrapped)
        {
            length += oldCols;
        }
        else
        {
            const terminal_cell *lastRow = SrcCells(end);
            uint32_t lastUsed = oldCols;
            while (lastUsed > 0 && IsBlankDefault(lastRow[lastUsed - 1]))
                --lastUsed;
            length += lastUsed;
        }

        uint32_t lineCursorLogicalCol = 0xFFFFFFFFu;
        if (cursorOnPrimary && cursorSrcRow >= start && cursorSrcRow <= end)
        {
            lineCursorLogicalCol = (cursorSrcRow - start) * oldCols + self.cursorCol;
        }

        uint32_t emitRows = (length + newCols - 1) / newCols;
        if (emitRows == 0)
            emitRows = 1;

        ASSERT((uint64_t)emitCount + emitRows <= maxEmitted);

        for (uint32_t er = 0; er < emitRows; ++er)
        {
            terminal_cell *dstRow = emitCells.data + (uint64_t)(emitCount + er) * newCols;
            uint32_t globalBase = er * newCols;
            for (uint32_t c = 0; c < newCols; ++c)
            {
                uint32_t globalCol = globalBase + c;
                if (globalCol < length)
                {
                    uint32_t srcRowOff = globalCol / oldCols;
                    uint32_t srcCol = globalCol % oldCols;
                    dstRow[c] = SrcCells(start + srcRowOff)[srcCol];
                }
                else
                {
                    dstRow[c] = defaultBlank;
                }
            }
            // Wrapped if not the last emitted row of this logical line, or if the source line itself
            // was tail-wrapped (only possible at the very end of the stream).
            uint8_t w = (er + 1 < emitRows) ? 1 : (tailWrapped ? 1 : 0);
            emitWrap[emitCount + er] = w;
        }

        if (lineCursorLogicalCol != 0xFFFFFFFFu)
        {
            uint32_t newRowOff = lineCursorLogicalCol / newCols;
            uint32_t newColOff = lineCursorLogicalCol % newCols;
            if (newRowOff >= emitRows)
            {
                newRowOff = emitRows - 1;
                newColOff = newCols - 1;
            }
            cursorEmitRow = emitCount + newRowOff;
            cursorEmitCol = newColOff;
        }

        emitCount += emitRows;
    }

    // Distribute emitted rows: last newRows go to primary, prior rows to scrollback (oldest dropped if > cap).
    uint32_t newScrollbackCap = self.scrollbackCap;
    uint32_t primaryFillCount;
    uint32_t newPrimaryEmitStart;
    uint32_t newScrollbackCount;
    uint32_t scrollbackEmitStart;

    if (emitCount <= newRows)
    {
        primaryFillCount = emitCount;
        newPrimaryEmitStart = 0;
        newScrollbackCount = 0;
        scrollbackEmitStart = 0;
    }
    else
    {
        primaryFillCount = newRows;
        newPrimaryEmitStart = emitCount - newRows;
        uint32_t fullScrollback = newPrimaryEmitStart;
        if (fullScrollback > newScrollbackCap)
        {
            scrollbackEmitStart = fullScrollback - newScrollbackCap;
            newScrollbackCount = newScrollbackCap;
        }
        else
        {
            scrollbackEmitStart = 0;
            newScrollbackCount = fullScrollback;
        }
    }

    self.cols = newCols;
    self.rows = newRows;
    self.scrollbackHead = (newScrollbackCap > 0 && newScrollbackCount < newScrollbackCap) ? newScrollbackCount : 0;
    self.scrollbackCount = newScrollbackCount;

    RegionAlloc::Reset(self.screenAlloc);
    AllocBuffers(self);

    // Copy primary content rows from emit buffer.
    for (uint32_t r = 0; r < primaryFillCount; ++r)
    {
        MemCpy(self.primary.data + (uint64_t)r * newCols,
               emitCells.data + (uint64_t)(newPrimaryEmitStart + r) * newCols,
               sizeof(terminal_cell) * newCols);
        self.primaryWrapped[r] = emitWrap[newPrimaryEmitStart + r];
    }

    // Copy scrollback rows.
    for (uint32_t r = 0; r < newScrollbackCount; ++r)
    {
        MemCpy(self.scrollback.data + (uint64_t)r * newCols,
               emitCells.data + (uint64_t)(scrollbackEmitStart + r) * newCols,
               sizeof(terminal_cell) * newCols);
        self.scrollbackWrapped[r] = emitWrap[scrollbackEmitStart + r];
    }

    self.cells = self.inAltScreen ? self.alt : self.primary;
    self.activeWrapped = self.inAltScreen ? self.altWrapped : self.primaryWrapped;

    if (cursorOnPrimary && cursorEmitRow != 0xFFFFFFFFu)
    {
        if (cursorEmitRow >= newPrimaryEmitStart && cursorEmitRow < newPrimaryEmitStart + primaryFillCount)
        {
            self.cursorRow = cursorEmitRow - newPrimaryEmitStart;
            self.cursorCol = cursorEmitCol;
        }
        else
        {
            // Cursor's logical line slid into scrollback or fell off entirely. Park at the last visible row.
            self.cursorRow = primaryFillCount > 0 ? primaryFillCount - 1 : 0;
            self.cursorCol = 0;
        }
    }
    else
    {
        if (self.cursorRow >= self.rows)
            self.cursorRow = self.rows - 1;
        if (self.cursorCol >= self.cols)
            self.cursorCol = self.cols - 1;
    }

    for (uint32_t k = 0; k < 2; ++k)
    {
        if (self.savedCursorRow[k] >= self.rows)
            self.savedCursorRow[k] = self.rows - 1;
        if (self.savedCursorCol[k] >= self.cols)
            self.savedCursorCol[k] = self.cols - 1;
    }
    self.scrollTop = 0;
    self.scrollBot = self.rows - 1;

    RegionAlloc::Destroy(temp);
}

void API Feed(terminal_screen &self, byteview bytes)
{
    for (uint64_t i = 0; i < bytes.size; ++i)
    {
        uint8_t b = bytes.data[i];

        switch (self.state)
        {
        case parser_state::Ground:
            StepGround(self, b);
            break;

        case parser_state::Esc:
            if (b == '[')
            {
                ResetCsi(self);
                self.state = parser_state::Csi;
            }
            else if (b == 'M')
            {
                // Reverse Index — at top of scrolling region, scroll content down by 1; otherwise move cursor up.
                if (self.cursorRow == self.scrollTop)
                    ScrollDown(self);
                else if (self.cursorRow > 0)
                    --self.cursorRow;
                self.state = parser_state::Ground;
            }
            else if (b == '7')
            {
                uint32_t slot = self.inAltScreen ? 1 : 0;
                self.savedCursorCol[slot] = self.cursorCol;
                self.savedCursorRow[slot] = self.cursorRow;
                self.savedFg[slot] = self.curFg;
                self.savedBg[slot] = self.curBg;
                self.savedAttrs[slot] = self.curAttrs;
                self.state = parser_state::Ground;
            }
            else if (b == '8')
            {
                uint32_t slot = self.inAltScreen ? 1 : 0;
                self.cursorCol = self.savedCursorCol[slot];
                self.cursorRow = self.savedCursorRow[slot];
                self.curFg = self.savedFg[slot];
                self.curBg = self.savedBg[slot];
                self.curAttrs = self.savedAttrs[slot];
                if (self.cursorRow >= self.rows)
                    self.cursorRow = self.rows - 1;
                if (self.cursorCol >= self.cols)
                    self.cursorCol = self.cols - 1;
                self.state = parser_state::Ground;
            }
            else if (b == 'c')
            {
                // RIS — full reset of cursor + SGR + region + screen.
                self.cursorCol = 0;
                self.cursorRow = 0;
                self.curFg = self.defaultFg;
                self.curBg = self.defaultBg;
                self.scrollTop = 0;
                self.scrollBot = self.rows - 1;
                self.applicationCursorKeys = false;
                self.cursorVisible = true;
                if (self.inAltScreen)
                    LeaveAltScreen(self);
                EraseRange(self, 0, self.cols * self.rows);
                for (uint32_t r = 0; r < self.rows; ++r)
                    self.activeWrapped[r] = 0;
                self.state = parser_state::Ground;
            }
            else if (b == '(' || b == ')' || b == '*' || b == '+')
            {
                // G0..G3 charset designation: consume the designator byte that follows.
                self.state = parser_state::EscCharset;
            }
            else if (b == '=' || b == '>')
            {
                // DECKPAM/DECKPNM keypad mode — accept silently.
                self.state = parser_state::Ground;
            }
            else if (b == ']')
            {
                self.state = parser_state::Osc;
            }
            else if (b == 'P')
            {
                // DCS payload (DECRQSS replies, Sixel etc); ST-terminated. Swallow.
                self.state = parser_state::Dcs;
            }
            else
            {
                LOG("terminal_screen: unhandled ESC byte 0x%x"_s, (uint32_t)b);
                self.state = parser_state::Ground;
            }
            break;

        case parser_state::EscCharset:
            // Designator byte (e.g. 'B' for ASCII, '0' for line drawing); we treat as no-op.
            self.state = parser_state::Ground;
            break;

        case parser_state::Osc:
            // Swallow OSC payload. Terminator is BEL (0x07) or ST (ESC \).
            if (b == 0x07)
                self.state = parser_state::Ground;
            else if (b == 0x1B)
                self.state = parser_state::OscEsc;
            // else: keep swallowing
            break;

        case parser_state::OscEsc:
            // Either '\\' completes ST, or any other byte aborts the OSC and re-enters Esc.
            if (b == '\\')
                self.state = parser_state::Ground;
            else
                self.state = parser_state::Osc;
            break;

        case parser_state::Dcs:
            // Swallow DCS payload until ST (ESC '\\').
            if (b == 0x1B)
                self.state = parser_state::DcsEsc;
            // else: keep swallowing (incl. raw 0x9C which we don't enter via 8-bit path)
            break;

        case parser_state::DcsEsc:
            if (b == '\\')
                self.state = parser_state::Ground;
            else
                self.state = parser_state::Dcs;
            break;

        case parser_state::Csi:
            if (b == '?' && self.paramCount == 0 && !self.paramHasDigits)
            {
                self.csiPrivate = true;
            }
            else if (b >= '0' && b <= '9')
            {
                if (!self.paramHasDigits)
                {
                    if (self.paramCount < kMaxParams)
                        self.params[self.paramCount] = 0;
                    self.paramHasDigits = true;
                }
                if (self.paramCount < kMaxParams)
                    self.params[self.paramCount] = self.params[self.paramCount] * 10 + (b - '0');
            }
            else if (b == ';' || b == ':')
            {
                // Colon is the ITU T.416 subparam separator (e.g. SGR 4:3, 38:2:r:g:b);
                // we don't model subparam grouping — flatten to ';' so primary args still land.
                if (self.paramHasDigits)
                    ++self.paramCount;
                self.paramHasDigits = false;
                if (self.paramCount < kMaxParams)
                    self.params[self.paramCount] = 0xFFFFFFFFu;
            }
            else if (b >= 0x20 && b <= 0x2F)
            {
                // Intermediate byte (DECSCUSR ' ', DECRQM '$', SGR ITU '!', ...). Mark and stay.
                self.csiIntermediate = true;
            }
            else if (b >= 0x40 && b <= 0x7E)
            {
                if (self.paramHasDigits)
                    ++self.paramCount;
                if (self.csiPrivate)
                {
                    if (b == 'h' || b == 'l')
                        DispatchPrivateMode(self, b == 'h');
                }
                else
                    DispatchCsi(self, b);
                self.state = parser_state::Ground;
            }
            else
            {
                // Unsupported byte: drop and stay in CSI until a final.
            }
            break;
        }
    }
}

auto API Cols(const terminal_screen &self) -> uint32_t { return self.cols; }
auto API Rows(const terminal_screen &self) -> uint32_t { return self.rows; }
auto API CursorCol(const terminal_screen &self) -> uint32_t { return self.cursorCol; }
auto API CursorRow(const terminal_screen &self) -> uint32_t { return self.cursorRow; }
auto API CursorVisible(const terminal_screen &self) -> bool { return self.cursorVisible; }
auto API ApplicationCursorKeys(const terminal_screen &self) -> bool { return self.applicationCursorKeys; }

auto API CellAt(const terminal_screen &self, uint32_t col, uint32_t row) -> terminal_cell
{
    return self.cells[Idx(self, col, row)];
}

auto API ScrollbackCount(const terminal_screen &self) -> uint32_t { return self.scrollbackCount; }

auto API ScrollbackCellAt(const terminal_screen &self, uint32_t col, uint32_t line) -> terminal_cell
{
    // line=0 is oldest; map to ring slot relative to head.
    uint32_t oldestSlot = (self.scrollbackHead + self.scrollbackCap - self.scrollbackCount) % self.scrollbackCap;
    uint32_t slot = (oldestSlot + line) % self.scrollbackCap;
    return self.scrollback[(uint64_t)slot * self.cols + col];
}

} // namespace TerminalScreen

} // namespace nyla
