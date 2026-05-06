#pragma once

#include <cstdint>

#include "nyla/commons/engine.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct ui_frame_input
{
    bool navUp;
    bool navDown;
    bool navHome;
    bool navEnd;
    bool navPgUp;
    bool navPgDn;
    bool navActivate;
    bool modalConfirm; // edge-triggered; only meaningful while a modal is open
    bool modalCancel;
    int32_t pgStep;
    byteview textChars; // utf8 bytes typed this frame; control codes (BS/Enter) interpreted by widgets

    // Pointer (mouse) state in widget coords. For cell-renderer apps the caller converts
    // engine_frame's pixel-space pointer into cell coords before feeding it here. For a
    // pixel-precise backend the caller passes the pixel coords through unchanged.
    // pointerButtons bit 0 = left, 1 = middle, 2 = right (X11 button id - 1).
    int32_t pointerX;
    int32_t pointerY;
    uint32_t pointerButtons;
    uint32_t pointerPress;
    uint32_t pointerRelease;
};

// Input pump state — held inside ui_state. Tracks per-key edge + repeat for
// the slots claimed by Ui::BootstrapInput (Up/Down repeat; Enter/Escape/Y/N
// are pure edge).
struct ui_pump_state
{
    uint16_t prev; // bit i = slot i pressed last frame
    uint64_t holdStartUs[2];
    uint64_t lastRepeatUs[2];
};

// Palette consulted by widgets for their resting paint. Caller-owned: assign
// `ui_state.theme` before `Begin` (or leave zero — `Begin` applies the default
// when it sees zero). Semantic colors that are caller concerns (delete row,
// staged row, etc.) stay in the caller.
struct ui_theme
{
    uint32_t bg;        // background fill behind text/widgets
    uint32_t focusedFg; // foreground for the widget that owns focus this frame
};

constexpr ui_theme kDefaultUiTheme{
    .bg = 0xFF1C1C1Cu,
    .focusedFg = 0xFFCCCCCCu,
};

// Per-window persistent state. Slot is allocated on first BeginWindow per id and never
// recycled — the x/y fields back tunables registrations whose ptrs must stay valid for
// the lifetime of ui_state.
struct ui_window_slot
{
    uint32_t id;
    int32_t x, y;
    int32_t w, h;    // content rect (title row not included in h)
    uint32_t zOrder; // higher = drawn on top; refreshed on click-to-front
    uint32_t flags;  // mirror of ui_window_desc.flags from this/last frame
    // Item-array ranges occupied by widgets registered inside this window. Used for
    // modal-scope focus restriction and focus-on-open. prev* covers prev frame, cur* this.
    uint32_t prevItemsBegin, prevItemsEnd;
    uint32_t curItemsBegin, curItemsEnd;
    bool presentThisFrame;    // any BeginWindow for this id seen this frame
    bool wasPresentLastFrame; // snapshot of presentThisFrame at frame Begin
    bool openedJustNow;       // first appeared this frame (focus-on-open trigger)
};

struct ui_state
{
    static constexpr uint32_t kIdStackMax = 16;
    static constexpr uint32_t kItemsMax = 4096;
    static constexpr uint32_t kWindowSlotsMax = 16; // cap chosen to leave tunables headroom (2 ints/slot)

    ui_theme theme;

    uint32_t idStack[kIdStackMax];
    uint8_t idDepth;

    uint32_t itemsA[kItemsMax];
    uint32_t itemsB[kItemsMax];
    uint32_t *curItems;
    uint32_t *prevItems;
    uint32_t curCount;
    uint32_t prevCount;

    uint32_t focusId;
    uint32_t hotId;    // widget under pointer this frame (set during the frame as widgets register)
    uint32_t activeId; // widget that received pointer-press; clears on release. Persists across frames while held.
    bool activeArmedActivate; // press landed on already-focused widget — Button uses this to require focus-then-click

    // Derived at Begin from prev frame's slots: any kWindowFlagModal window painted last
    // frame implies modal active. modalSlotIdx indexes into windows[] when modalActive.
    bool modalActive;
    uint32_t modalSlotIdx;
    bool inModalScope;      // current scope is inside a kWindowFlagModal window (per-frame, set during BeginWindow)
    bool savedInModalScope; // saved by BeginWindow, restored by EndWindow

    // Pointer state latched at Begin from ui_frame_input. Press edges are consumable
    // (widgets clear them when claiming the click) so only one widget activates per frame.
    int32_t pointerX;
    int32_t pointerY;
    uint32_t pointerButtons;
    uint32_t pointerPress;
    uint32_t pointerRelease;
    bool pointerSuppress; // background-widget gate while a modal is open

    bool navActivate;
    bool modalConfirm;
    bool modalCancel;
    bool textWidgetFocused; // any TextInput owns focus this frame; gates global action keys
    byteview textChars;

    uint32_t viewportCols;
    uint32_t viewportRows;
    int32_t cursorX, cursorY;
    int32_t lineStartX;
    int32_t lastItemX, lastItemY, lastItemW, lastItemH;
    int32_t lineH;

    // Window pool. `windowCount` only grows; slots stay alive even when not painted this
    // frame so persisted x/y survive (and tunable ptrs stay valid).
    ui_window_slot windows[kWindowSlotsMax];
    uint32_t windowCount;
    uint32_t nextZOrder;
    uint32_t hoveredWindowId;  // top-most window under pointer at frame Begin (prev-frame z-order)
    uint32_t draggingWindowId; // non-zero while title-bar drag in progress
    int32_t dragOffsetX, dragOffsetY;

    // Saved by BeginWindow, restored by EndWindow.
    uint32_t openWindowId;
    int32_t savedCursorX, savedCursorY;
    int32_t savedLineStartX;
    int32_t savedLineH;
    bool savedPointerSuppress;

    ui_pump_state pump;
};

struct ui_selectable_result
{
    bool focused;
    bool activated;
};

// TextInput is intentionally minimal: append-only line, no cursor positioning, no selection,
// no IME. `len` counts utf8 BYTES, not codepoints — cursor visualization in the caller will
// land mid-codepoint for non-ASCII. Suitable for ASCII filenames / search filters; NOT a
// drop-in for general-purpose text editing.
struct ui_text_input_result
{
    bool focused;
    bool changed;
    bool activated;
};

struct ui_modal_result
{
    bool confirm;
    bool cancel;
};

// Window flag bits for ui_window_desc.flags.
//   kWindowFlagModal: while painted, this window is the active modal. Forces top z each
//   frame, gates background widgets (nav/click suppressed), restricts focus cycling to
//   widgets registered inside the window. Y/N edges flow through Ui::ModalConfirmCancel.
inline constexpr uint32_t kWindowFlagModal = 1u << 0;

// Window: persistent draggable panel. See BeginWindow doc inside Ui namespace below.
struct ui_window_desc
{
    uint32_t flags;             // kWindowFlag* bitfield
    int32_t initialX, initialY; // used only the first time the slot is allocated
    int32_t w, h;               // content size (title row outside)
    byteview title;
};

namespace Ui
{

// Claims input_id::Custom1..Custom6 for UI navigation:
//   Custom1=Up, Custom2=Down, Custom3=Enter, Custom4=Escape, Custom5=Y, Custom6=N.
// App is free to use Custom7..Custom15. Call once at boot before Pump.
void API BootstrapInput();

// Builds ui_frame_input from engine_frame: edge-detects + key-repeats nav,
// edge-detects activate/confirm/cancel, forwards textChars. pgStep is the
// caller's visible-row count (kept for forward-compat once Page keys land).
auto API Pump(ui_state &s, const engine_frame &frame, int32_t pgStep) -> ui_frame_input;

void API Begin(ui_state &s, const ui_frame_input &in, uint32_t viewportCols, uint32_t viewportRows);
void API End(ui_state &s);

void API PushId(ui_state &s, uint32_t id);
void API PushId(ui_state &s, byteview key); // hashes key bytes; same id-stack as the uint32_t form
void API PopId(ui_state &s);
auto API MakeId(const ui_state &s, uint32_t local) -> uint32_t;

// True if the pointer is inside the rectangle and the modal gate doesn't suppress hits this frame.
auto API ItemHitTest(const ui_state &s, int32_t x, int32_t y, int32_t w, int32_t h) -> bool;

void API SetCursor(ui_state &s, int32_t x, int32_t y);
void API GetCursor(const ui_state &s, int32_t &x, int32_t &y);
void API NewLine(ui_state &s);
void API SameLine(ui_state &s, int32_t spacing);

void API Text(ui_state &s, byteview text, uint32_t fg);

auto API Selectable(ui_state &s, uint32_t localId) -> ui_selectable_result;

// Selectable variant with a paint rect for mouse hit-testing. Use for list rows that are
// actually drawn this frame; pass paint-free Selectable for off-screen rows that should
// stay in the focus chain (so keyboard nav still reaches them) without click handling.
auto API SelectableHit(ui_state &s, uint32_t localId, int32_t x, int32_t y, int32_t w, int32_t h)
    -> ui_selectable_result;
auto API TextInput(ui_state &s, uint32_t localId, byteview prefix, uint8_t *buf, uint32_t cap, uint32_t &len,
                   uint32_t restingFg) -> ui_text_input_result;

// Clickable button. Paints `[ label ]` at cursor with theme.bg + (focused ? theme.focusedFg : restingFg).
// Registers in the focus chain like Selectable; returns true on the frame navActivate fires while focused.
auto API Button(ui_state &s, uint32_t localId, byteview label, uint32_t restingFg) -> bool;

// True if any TextInput claimed focus this frame. Use to gate letter-keyed global actions
// (e.g. don't fire 'S = Save' while typing in a filter).
auto API IsTextWidgetFocused(const ui_state &s) -> bool;

// Window: persistent draggable panel. Slot allocated on first call per id; subsequent
// calls re-use it. Title bar paints at row y, content area at rows y+1..y+h (clipped).
// Click on title bar = drag + raise to top z. Inside the BeginWindow/EndWindow scope,
// cursor starts at (x, y+1), lineStartX = x, and CellRenderer clip is pushed so painting
// outside the body rect is dropped. Returns true (collapse deferred — see ITERATION).
//
// Hit-test gating: only the topmost window under pointer (computed at frame Begin from
// previous frame's slot rects + zOrder) lets its widgets receive clicks. Non-topmost
// scopes set pointerSuppress, mirroring the modal mechanism.
auto API BeginWindow(ui_state &s, uint32_t localId, const ui_window_desc &desc) -> bool;
void API EndWindow(ui_state &s);

// Read + consume modal Y / Esc-or-N edges. Only fires while a kWindowFlagModal window is
// active (painted last frame). Caller drives the open/close lifecycle through their own
// flag — paint the modal window when wanted, stop painting it on confirm/cancel.
auto API ModalConfirmCancel(ui_state &s) -> ui_modal_result;

} // namespace Ui

} // namespace nyla
