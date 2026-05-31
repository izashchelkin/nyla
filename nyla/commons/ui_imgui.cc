#include "nyla/commons/ui_imgui.h"

#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/hash.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/tunables.h"

namespace nyla
{

// ─── Paint backend shim ────────────────────────────────────────────────────
// All widget painting routes through these functions.
// Currently wraps CellRenderer; swapping to GPU-native means changing THIS block only.
namespace UiPaint
{
INLINE void Text(uint32_t col, uint32_t row, byteview text, uint32_t fg, uint32_t bg)
{
    CellRenderer::Text(col, row, text, fg, bg);
}
INLINE void PutCell(uint32_t col, uint32_t row, cell_attr attr)
{
    CellRenderer::PutCell(col, row, attr);
}
INLINE void PushClip(int32_t col, int32_t row, int32_t w, int32_t h)
{
    CellRenderer::PushClip(col, row, w, h);
}
INLINE void PopClip()
{
    CellRenderer::PopClip();
}
// Fill a rectangular region with a solid color using background cells.
INLINE void FillRect(int32_t col, int32_t row, int32_t w, int32_t h, uint32_t rgba)
{
    for (int32_t r = 0; r < h; ++r)
        for (int32_t c = 0; c < w; ++c)
            CellRenderer::Text((uint32_t)(col + c), (uint32_t)(row + r), " "_s, rgba, rgba);
}
} // namespace UiPaint

namespace Ui
{

namespace
{

// Pump slot layout — index into ui_pump_state.prev / hold / repeat arrays.
// PageUp/PageDown/Home/End are deliberately omitted: KeyPhysical lacks them
// today; add slots when the platform layer surfaces those keys.
enum pump_slot : uint8_t
{
    SlotUp = 0,
    SlotDown,
    SlotEnter,
    SlotEscape,
    SlotY,
    SlotN,
    SlotCount,
};
static_assert(SlotCount <= 16, "pump_slot count limited by ui_pump_state.prev bitfield");
constexpr uint8_t kRepeatSlotCount = 2; // Up, Down

constexpr input_id kPumpInputs[SlotCount] = {
    input_id::Custom1, input_id::Custom2, input_id::Custom3, input_id::Custom4, input_id::Custom5, input_id::Custom6,
};

constexpr KeyPhysical kPumpKeys[SlotCount] = {
    KeyPhysical::ArrowUp, KeyPhysical::ArrowDown, KeyPhysical::Enter,
    KeyPhysical::Escape,  KeyPhysical::Y,         KeyPhysical::N,
};

constexpr uint64_t kRepeatDelayUs = 350'000;
constexpr uint64_t kRepeatRateUs = 30'000;

auto FindIndex(const uint32_t *arr, uint32_t count, uint32_t needle) -> uint32_t
{
    for (uint32_t i = 0; i < count; ++i)
        if (arr[i] == needle)
            return i;
    return count;
}

void RecordItem(ui_state &s, int32_t x, int32_t y, int32_t w, int32_t h)
{
    s.lastItemX = x;
    s.lastItemY = y;
    s.lastItemW = w;
    s.lastItemH = h;
    if (h > s.lineH)
        s.lineH = h;
}

auto FindWindowSlot(ui_state &s, uint32_t id) -> ui_window_slot *
{
    for (uint32_t i = 0; i < s.windowCount; ++i)
        if (s.windows[i].id == id)
            return &s.windows[i];
    return nullptr;
}

// True when current scope is a background widget while a modal window is up; widgets
// gate their activation on this so only widgets registered inside the modal can fire.
auto ScopeBlocked(const ui_state &s) -> bool
{
    return s.modalActive && !s.inModalScope;
}

void ClampWindowPos(ui_state &s, ui_window_slot &slot)
{
    constexpr int32_t kMinVisible = 8;
    int32_t maxX = (int32_t)s.viewportCols - kMinVisible;
    int32_t minX = -(slot.w - kMinVisible);
    if (minX > maxX)
        minX = maxX;
    if (slot.x < minX)
        slot.x = minX;
    if (slot.x > maxX)
        slot.x = maxX;
    int32_t maxY = (int32_t)s.viewportRows - 1;
    if (slot.y < 0)
        slot.y = 0;
    if (slot.y > maxY)
        slot.y = maxY;
}

} // namespace

void BootstrapInput()
{
    for (uint8_t i = 0; i < SlotCount; ++i)
        InputManager::Map(kPumpInputs[i], input_interface_type::Keyboard, (uint32_t)kPumpKeys[i]);
}

auto Pump(ui_state &s, const engine_frame &frame, int32_t pgStep) -> ui_frame_input
{
    bool pressed[SlotCount];
    for (uint8_t i = 0; i < SlotCount; ++i)
        pressed[i] = InputManager::IsPressed(kPumpInputs[i]);

    auto wasPrev = [&](uint8_t i) -> bool { return (s.pump.prev & (uint16_t)(1u << i)) != 0; };
    auto edge = [&](uint8_t i) -> bool { return pressed[i] && !wasPrev(i); };

    auto trig = [&](uint8_t slotIdx, uint8_t repeatIdx) -> bool {
        bool down = pressed[slotIdx];
        bool prev = wasPrev(slotIdx);
        if (down && !prev)
        {
            s.pump.holdStartUs[repeatIdx] = frame.frameStartUs;
            s.pump.lastRepeatUs[repeatIdx] = frame.frameStartUs;
            return true;
        }
        if (down && frame.frameStartUs - s.pump.holdStartUs[repeatIdx] >= kRepeatDelayUs &&
            frame.frameStartUs - s.pump.lastRepeatUs[repeatIdx] >= kRepeatRateUs)
        {
            s.pump.lastRepeatUs[repeatIdx] = frame.frameStartUs;
            return true;
        }
        return false;
    };

    ui_frame_input in{
        .navUp = trig(SlotUp, 0),
        .navDown = trig(SlotDown, 1),
        .navHome = false,
        .navEnd = false,
        .navPgUp = false,
        .navPgDn = false,
        .navActivate = edge(SlotEnter),
        .modalConfirm = edge(SlotY),
        .modalCancel = edge(SlotEscape) || edge(SlotN),
        .pgStep = pgStep,
        .textChars = frame.textChars,
    };

    uint16_t newPrev = 0;
    for (uint8_t i = 0; i < SlotCount; ++i)
        if (pressed[i])
            newPrev |= (uint16_t)(1u << i);
    s.pump.prev = newPrev;

    return in;
}

void PushId(ui_state &s, uint32_t id)
{
    ASSERT(s.idDepth < ui_state::kIdStackMax);
    s.idStack[s.idDepth++] = id;
}

void PushId(ui_state &s, byteview key)
{
    PushId(s, HashBytes32(key));
}

void PopId(ui_state &s)
{
    ASSERT(s.idDepth > 0);
    --s.idDepth;
}

auto MakeId(const ui_state &s, uint32_t local) -> uint32_t
{
    uint32_t h = kHashInit32;
    for (uint8_t i = 0; i < s.idDepth; ++i)
        h = HashCombine32(h, s.idStack[i]);
    h = HashCombine32(h, local);
    if (h == 0)
        h = 1; // reserve 0 for "no id"
    return h;
}

void Begin(ui_state &s, const ui_frame_input &in, uint32_t viewportCols, uint32_t viewportRows)
{
    if (s.theme.bg == 0)
        s.theme = kDefaultUiTheme;

    if (!s.curItems)
    {
        s.curItems = s.itemsA;
        s.prevItems = s.itemsB;
        s.curCount = 0;
        s.prevCount = 0;
    }
    else
    {
        uint32_t *tmp = s.prevItems;
        s.prevItems = s.curItems;
        s.prevCount = s.curCount;
        s.curItems = tmp;
        s.curCount = 0;
    }

    s.idDepth = 0;
    s.hotId = 0;
    s.textWidgetFocused = false;
    s.inModalScope = false;

    s.viewportCols = viewportCols;
    s.viewportRows = viewportRows;
    s.cursorX = 0;
    s.cursorY = 0;
    s.lineStartX = 0;
    s.lineH = 0;
    s.lastItemX = 0;
    s.lastItemY = 0;
    s.lastItemW = 0;
    s.lastItemH = 0;

    // Slot frame swap: snapshot prev-frame presence (for hovered scan + modal detect),
    // archive prev-frame item ranges, reset cur ranges + per-frame flags.
    for (uint32_t i = 0; i < s.windowCount; ++i)
    {
        ui_window_slot &w = s.windows[i];
        w.wasPresentLastFrame = w.presentThisFrame;
        w.presentThisFrame = false;
        w.prevItemsBegin = w.curItemsBegin;
        w.prevItemsEnd = w.curItemsEnd;
        w.curItemsBegin = 0;
        w.curItemsEnd = 0;
        w.openedJustNow = false;
    }

    // List slot frame swap: archive prev-frame ranges, reset cur. Slots are persistent like
    // window slots — never deleted, so prev range survives across frames where a list's
    // BeginList wasn't called (the prev range will simply be empty next time).
    for (uint32_t i = 0; i < s.listCount; ++i)
    {
        ui_list_slot &l = s.lists[i];
        l.prevItemsBegin = l.curItemsBegin;
        l.prevItemsEnd = l.curItemsEnd;
        l.curItemsBegin = 0;
        l.curItemsEnd = 0;
        l.presentThisFrame = false;
    }

    // Modal active = any kWindowFlagModal slot painted last frame.
    s.modalActive = false;
    s.modalSlotIdx = 0;
    for (uint32_t i = 0; i < s.windowCount; ++i)
    {
        if (s.windows[i].wasPresentLastFrame && (s.windows[i].flags & kWindowFlagModal))
        {
            s.modalActive = true;
            s.modalSlotIdx = i;
            break;
        }
    }

    // navActivate / textChars latch unconditionally; per-scope ScopeBlocked() gate inside
    // each widget keeps modal-mode background widgets inert.
    s.navActivate = in.navActivate;
    s.textChars = in.textChars;
    s.modalConfirm = s.modalActive ? in.modalConfirm : false;
    s.modalCancel = s.modalActive ? in.modalCancel : false;

    s.pointerX = in.pointerX;
    s.pointerY = in.pointerY;
    s.pointerButtons = in.pointerButtons;
    s.pointerPress = in.pointerPress;
    s.pointerRelease = in.pointerRelease;
    // Press always flows in — root widgets respect pointerSuppress (set below) so a modal
    // gates the background. Zeroing the press here would also gate a modal *window*'s own
    // title-bar drag, since the modal window paints over a modal-active root.
    // activeId clear deferred to End(): widgets need s.activeId == id while they evaluate
    // clickActivate this frame; clearing here breaks click-release activation.

    // Wheel edges from pointerPress bits 3 (button 4 = wheel up) and 4 (button 5 = wheel down).
    // These are consumable — the first scrollable child to use them clears the edge.
    s.wheelEdgeUp = (s.pointerPress & (1u << 3)) != 0;
    s.wheelEdgeDn = (s.pointerPress & (1u << 4)) != 0;

    // Drag handling — must precede hovered-window compute so the dragged window stays under
    // the pointer. Drag clears on any frame where button 0 isn't held.
    if (s.draggingWindowId != 0)
    {
        if (!(s.pointerButtons & 1u))
        {
            s.draggingWindowId = 0;
        }
        else if (ui_window_slot *slot = FindWindowSlot(s, s.draggingWindowId))
        {
            slot->x = s.pointerX - s.dragOffsetX;
            slot->y = s.pointerY - s.dragOffsetY;
            ClampWindowPos(s, *slot);
        }
    }

    // Top-most window under pointer from prev-frame presence + zOrder. Modal forces top z
    // each frame inside BeginWindow, so when modalActive the modal naturally wins this scan.
    s.hoveredWindowId = 0;
    {
        uint32_t bestZ = 0;
        for (uint32_t i = 0; i < s.windowCount; ++i)
        {
            const ui_window_slot &w = s.windows[i];
            if (!w.wasPresentLastFrame)
                continue;
            if (s.pointerX < w.x || s.pointerX >= w.x + w.w)
                continue;
            if (s.pointerY < w.y || s.pointerY > w.y + w.h)
                continue;
            if (s.hoveredWindowId == 0 || w.zOrder > bestZ)
            {
                s.hoveredWindowId = w.id;
                bestZ = w.zOrder;
            }
        }
    }

    // Root-scope suppression: a modal or any window covering the pointer absorbs root hits.
    // BeginWindow re-derives suppression for its own scope.
    s.pointerSuppress = s.modalActive || (s.hoveredWindowId != 0);

    // Focus nav. When modalActive, restrict cycling to the modal slot's prev-frame item
    // range so focus stays inside the dialog. Otherwise cycle across the flat prev list,
    // which threads through every window scope in registration order.
    uint32_t lo = 0;
    uint32_t hi = s.prevCount;
    if (s.modalActive)
    {
        const ui_window_slot &m = s.windows[s.modalSlotIdx];
        lo = m.prevItemsBegin;
        hi = m.prevItemsEnd;
    }
    if (hi > lo)
    {
        uint32_t idx = lo;
        for (uint32_t i = lo; i < hi; ++i)
        {
            if (s.prevItems[i] == s.focusId)
            {
                idx = i;
                break;
            }
        }

        int64_t newIdx = (int64_t)idx;
        if (in.navHome)
            newIdx = (int64_t)lo;
        else if (in.navEnd)
            newIdx = (int64_t)hi - 1;
        else
        {
            if (in.navUp)
                --newIdx;
            if (in.navDown)
                ++newIdx;
            if (in.navPgUp)
                newIdx -= in.pgStep;
            if (in.navPgDn)
                newIdx += in.pgStep;
        }

        if (newIdx < (int64_t)lo)
            newIdx = (int64_t)lo;
        if (newIdx >= (int64_t)hi)
            newIdx = (int64_t)hi - 1;
        s.focusId = s.prevItems[newIdx];
    }
}

void End(ui_state &s)
{
    if (s.pointerRelease & 1u)
    {
        s.activeId = 0;
        s.activeArmedActivate = false;
    }
    if (s.curCount == 0)
        return;
    if (FindIndex(s.curItems, s.curCount, s.focusId) >= s.curCount)
        s.focusId = s.curItems[0];
}

auto ItemHitTest(const ui_state &s, int32_t x, int32_t y, int32_t w, int32_t h) -> bool
{
    if (s.pointerSuppress)
        return false;
    return s.pointerX >= x && s.pointerX < x + w && s.pointerY >= y && s.pointerY < y + h;
}

void SetCursor(ui_state &s, int32_t x, int32_t y)
{
    s.cursorX = x;
    s.cursorY = y;
    s.lineStartX = x;
    s.lineH = 0;
    s.lastItemX = x;
    s.lastItemY = y;
    s.lastItemW = 0;
    s.lastItemH = 0;
}

void GetCursor(const ui_state &s, int32_t &x, int32_t &y)
{
    x = s.cursorX;
    y = s.cursorY;
}

void NewLine(ui_state &s)
{
    s.cursorX = s.lineStartX;
    s.cursorY += s.lineH > 0 ? s.lineH : 1;
    s.lineH = 0;
}

void SameLine(ui_state &s, int32_t spacing)
{
    s.cursorX = s.lastItemX + s.lastItemW + spacing;
    s.cursorY = s.lastItemY;
    if (s.lineH < s.lastItemH)
        s.lineH = s.lastItemH;
}

void Text(ui_state &s, byteview text, uint32_t fg)
{
    int32_t x = s.cursorX;
    int32_t y = s.cursorY;
    int32_t maxW = (int32_t)s.viewportCols - x;
    if (maxW < 0)
        maxW = 0;
    int32_t printable = text.size < (uint64_t)maxW ? (int32_t)text.size : maxW;
    if (printable > 0 && y >= 0 && y < (int32_t)s.viewportRows)
        UiPaint::Text((uint32_t)x, (uint32_t)y, byteview{text.data, (uint64_t)printable}, fg, s.theme.bg);
    RecordItem(s, x, y, printable, 1);
    NewLine(s);
}

auto Selectable(ui_state &s, uint32_t localId) -> ui_selectable_result
{
    uint32_t id = MakeId(s, localId);
    if (s.curCount < ui_state::kItemsMax)
        s.curItems[s.curCount++] = id;

    ui_selectable_result r{};
    r.focused = (id == s.focusId);
    if (r.focused && s.navActivate && !ScopeBlocked(s))
    {
        r.activated = true;
        s.navActivate = false; // consume
    }
    return r;
}

auto SelectableHit(ui_state &s, uint32_t localId, int32_t x, int32_t y, int32_t w, int32_t h) -> ui_selectable_result
{
    uint32_t id = MakeId(s, localId);
    if (s.curCount < ui_state::kItemsMax)
        s.curItems[s.curCount++] = id;

    bool hit = ItemHitTest(s, x, y, w, h);
    if (hit)
        s.hotId = id;
    if (hit && (s.pointerPress & 1u))
    {
        s.focusId = id;
        s.activeId = id;
        s.pointerPress &= ~1u; // consume so only one widget claims this click
    }
    bool clickActivate = hit && (s.pointerRelease & 1u) && s.activeId == id;

    ui_selectable_result r{};
    r.focused = (id == s.focusId);
    if (r.focused && s.navActivate && !ScopeBlocked(s))
    {
        r.activated = true;
        s.navActivate = false;
    }
    if (clickActivate)
        r.activated = true;
    return r;
}

auto Button(ui_state &s, uint32_t localId, byteview label, uint32_t restingFg, uint32_t flags) -> bool
{
    uint32_t id = MakeId(s, localId);

    // kButtonFlagNoFocus: skip focus chain registration, activate on press (not release).
    // Use for action buttons that should stay keyboard-navigation-inert.
    if (flags & kButtonFlagNoFocus)
    {
        int32_t x = s.cursorX;
        int32_t y = s.cursorY;
        int32_t maxW = (int32_t)s.viewportCols - x;
        if (maxW <= 0)
        {
            RecordItem(s, x, y, 0, 1);
            NewLine(s);
            return false;
        }

        uint8_t line[128];
        // Paint as colored background strip + label (no brackets).
        uint64_t copy = label.size;
        if (copy > sizeof(line) - 2)
            copy = sizeof(line) - 2;
        line[0] = ' ';
        MemCpy(line + 1, label.data, copy);
        line[1 + copy] = ' ';
        uint32_t total = (uint32_t)(2 + copy);
        if (total > (uint32_t)maxW)
            total = (uint32_t)maxW;

        bool hover = ItemHitTest(s, x, y, (int32_t)total, 1);
        bool activated = hover && (s.pointerPress & 1u);
        if (activated)
            s.pointerPress &= ~1u;

        if (total > 0 && y >= 0 && y < (int32_t)s.viewportRows)
        {
            uint32_t fg = restingFg;
            uint32_t bg = hover ? restingFg : s.theme.bg;
            // Fill background strip then paint label inverted on hover
            UiPaint::Text((uint32_t)x, (uint32_t)y, byteview{line, total}, hover ? s.theme.bg : restingFg,
                          hover ? restingFg : s.theme.bg);
        }
        RecordItem(s, x, y, (int32_t)total, 1);
        NewLine(s);
        return activated;
    }

    // Normal (focusable) button — existing behavior.
    if (s.curCount < ui_state::kItemsMax)
        s.curItems[s.curCount++] = id;

    int32_t x = s.cursorX;
    int32_t y = s.cursorY;
    int32_t maxW = (int32_t)s.viewportCols - x;
    if (maxW < 0)
        maxW = 0;

    uint32_t total = 0;
    uint8_t line[128];
    if (maxW > 0)
    {
        line[0] = ' ';
        uint64_t copy = label.size;
        if (copy > sizeof(line) - 2)
            copy = sizeof(line) - 2;
        MemCpy(line + 1, label.data, copy);
        line[1 + copy] = ' ';
        total = (uint32_t)(2 + copy);
        if (total > (uint32_t)maxW)
            total = (uint32_t)maxW;
    }

    bool hit = ItemHitTest(s, x, y, (int32_t)total, 1);
    if (hit)
        s.hotId = id;
    if (hit && (s.pointerPress & 1u))
    {
        // Two-step click: press only arms activation if the widget already owned focus.
        // First click on an unfocused button just shifts focus; the next click activates.
        s.activeArmedActivate = (s.focusId == id);
        s.focusId = id;
        s.activeId = id;
        s.pointerPress &= ~1u;
    }
    bool clickActivate = hit && (s.pointerRelease & 1u) && s.activeId == id && s.activeArmedActivate;

    bool focused = (id == s.focusId);
    bool activated = false;
    if (focused && s.navActivate && !ScopeBlocked(s))
    {
        activated = true;
        s.navActivate = false;
    }
    if (clickActivate)
        activated = true;

    if (total > 0 && y >= 0 && y < (int32_t)s.viewportRows)
    {
        // Focused button inverts: bg fills with the focused fg color, label paints in theme bg.
        // Mirrors the asset_tool list-row visual so focus is unambiguous regardless of restingFg.
        uint32_t fg = focused ? s.theme.bg : restingFg;
        uint32_t bg = focused ? s.theme.focusedFg : s.theme.bg;
        UiPaint::Text((uint32_t)x, (uint32_t)y, byteview{line, total}, fg, bg);
    }
    RecordItem(s, x, y, (int32_t)total, 1);
    NewLine(s);

    return activated;
}

auto TextInput(ui_state &s, uint32_t localId, byteview prefix, uint8_t *buf, uint32_t cap, uint32_t &len,
               uint32_t restingFg) -> ui_text_input_result
{
    uint32_t id = MakeId(s, localId);
    if (s.curCount < ui_state::kItemsMax)
        s.curItems[s.curCount++] = id;

    // Hit-test before paint — must use the cursor + viewport, not lastItem (lastItem
    // reflects the previous widget at this point). TextInput always paints to viewport
    // edge, so the full row width is the right rect.
    int32_t hitX = s.cursorX;
    int32_t hitY = s.cursorY;
    int32_t hitW = (int32_t)s.viewportCols - hitX;
    if (hitW < 0)
        hitW = 0;
    bool hit = ItemHitTest(s, hitX, hitY, hitW, 1);
    if (hit)
        s.hotId = id;
    if (hit && (s.pointerPress & 1u))
    {
        s.focusId = id;
        s.activeId = id;
        s.pointerPress &= ~1u;
    }

    ui_text_input_result r{};
    r.focused = (id == s.focusId);
    if (r.focused)
        s.textWidgetFocused = true;

    if (r.focused && s.textChars.size > 0 && !ScopeBlocked(s))
    {
        for (uint64_t i = 0; i < s.textChars.size; ++i)
        {
            uint8_t c = s.textChars.data[i];
            if (c == 0x08 || c == 0x7F) // Backspace / Delete
            {
                if (len > 0)
                {
                    --len;
                    r.changed = true;
                }
                continue;
            }
            if (c == 0x0A || c == 0x0D) // Enter
            {
                r.activated = true;
                continue;
            }
            if (c < 0x20)
                continue; // skip other controls
            if (len < cap)
            {
                buf[len++] = c;
                r.changed = true;
            }
        }
        s.textChars = {}; // consume
    }

    int32_t x = s.cursorX;
    int32_t y = s.cursorY;
    int32_t maxW = (int32_t)s.viewportCols - x;
    if (maxW < 0)
        maxW = 0;
    int32_t paintedW = 0;
    if (maxW > 0 && y >= 0 && y < (int32_t)s.viewportRows)
    {
        uint8_t line[256];
        MemSet(line, ' ', sizeof(line));
        uint64_t p = prefix.size > sizeof(line) ? sizeof(line) : prefix.size;
        MemCpy(line, prefix.data, p);
        uint32_t copy = len;
        if (p + copy > sizeof(line) - 1)
            copy = (uint32_t)(sizeof(line) - 1 - p);
        MemCpy(line + p, buf, copy);
        if (r.focused)
            line[p + copy] = '_';
        uint32_t printable = (uint32_t)maxW < sizeof(line) ? (uint32_t)maxW : (uint32_t)sizeof(line);
        uint32_t fg = r.focused ? s.theme.focusedFg : restingFg;
        UiPaint::Text((uint32_t)x, (uint32_t)y, byteview{line, printable}, fg, s.theme.bg);
        paintedW = (int32_t)printable;
    }
    RecordItem(s, x, y, paintedW, 1);
    NewLine(s);

    return r;
}

auto IsTextWidgetFocused(const ui_state &s) -> bool
{
    return s.textWidgetFocused;
}

auto ModalConfirmCancel(ui_state &s) -> ui_modal_result
{
    ui_modal_result r{};
    if (!s.modalActive)
        return r;
    if (s.modalConfirm)
    {
        r.confirm = true;
        s.modalConfirm = false;
    }
    else if (s.modalCancel)
    {
        r.cancel = true;
        s.modalCancel = false;
    }
    return r;
}

auto BeginWindow(ui_state &s, uint32_t localId, const ui_window_desc &desc) -> bool
{
    uint32_t id = MakeId(s, localId);

    ui_window_slot *slot = FindWindowSlot(s, id);
    if (!slot)
    {
        if (s.windowCount >= ui_state::kWindowSlotsMax)
            return false; // out of slots — soft fail keeps the dev loop alive
        slot = &s.windows[s.windowCount++];
        slot->id = id;
        slot->x = desc.initialX;
        slot->y = desc.initialY;
        slot->zOrder = ++s.nextZOrder;

        // Persist position via tunables. Silent no-op if Tunables wasn't bootstrapped, or if
        // the registration cap is hit — pos just doesn't survive across runs in that case.
        uint8_t namebuf[24];
        uint64_t n = StringWriteFmt(span<uint8_t>{namebuf, sizeof(namebuf)}, "ui.win.%08x.x"_s, id);
        Tunables::RegisterInt(byteview{namebuf, n}, &slot->x, 1, -10000, 10000);
        n = StringWriteFmt(span<uint8_t>{namebuf, sizeof(namebuf)}, "ui.win.%08x.y"_s, id);
        Tunables::RegisterInt(byteview{namebuf, n}, &slot->y, 1, -10000, 10000);
    }

    slot->flags = desc.flags;
    slot->w = desc.w;
    slot->h = desc.h;
    if (desc.flags & kWindowFlagReposition)
    {
        slot->x = desc.initialX;
        slot->y = desc.initialY;
    }
    slot->openedJustNow = !slot->wasPresentLastFrame;
    slot->presentThisFrame = true;
    if (desc.flags & kWindowFlagModal)
        slot->zOrder = ++s.nextZOrder; // modal forces top each frame
    ClampWindowPos(s, *slot);

    bool topmost = (id == s.hoveredWindowId);
    int32_t tx = slot->x;
    int32_t ty = slot->y;
    int32_t tw = slot->w;
    int32_t th = slot->h;

    if (topmost && (s.pointerPress & 1u))
    {
        bool onTitleBar = s.pointerY == ty && s.pointerX >= tx && s.pointerX < tx + tw;
        bool onBody = s.pointerY > ty && s.pointerY <= ty + th && s.pointerX >= tx && s.pointerX < tx + tw;
        if (onTitleBar)
        {
            s.draggingWindowId = id;
            s.dragOffsetX = s.pointerX - slot->x;
            s.dragOffsetY = s.pointerY - slot->y;
            slot->zOrder = ++s.nextZOrder;
            s.pointerPress &= ~1u; // body widgets must not also see this press
        }
        else if (onBody)
        {
            slot->zOrder = ++s.nextZOrder; // click-to-front, but let the body widget keep the press
        }
    }

    // Title bar.
    {
        uint8_t line[256];
        MemSet(line, ' ', sizeof(line));
        uint64_t copy = desc.title.size > sizeof(line) ? sizeof(line) : desc.title.size;
        MemCpy(line, desc.title.data, copy);
        int32_t paintW = tw < (int32_t)sizeof(line) ? tw : (int32_t)sizeof(line);
        int32_t startCol = tx < 0 ? 0 : tx;
        int32_t endCol = tx + paintW;
        if (endCol > (int32_t)s.viewportCols)
            endCol = (int32_t)s.viewportCols;
        if (ty >= 0 && ty < (int32_t)s.viewportRows && endCol > startCol)
        {
            int32_t skip = startCol - tx;
            UiPaint::Text((uint32_t)startCol, (uint32_t)ty, byteview{line + skip, (uint64_t)(endCol - startCol)},
                          s.theme.bg, s.theme.focusedFg);
        }
    }

    // Body bg.
    {
        uint8_t line[256];
        MemSet(line, ' ', sizeof(line));
        int32_t paintW = tw < (int32_t)sizeof(line) ? tw : (int32_t)sizeof(line);
        int32_t startCol = tx < 0 ? 0 : tx;
        int32_t endCol = tx + paintW;
        if (endCol > (int32_t)s.viewportCols)
            endCol = (int32_t)s.viewportCols;
        int32_t skip = startCol - tx;
        for (int32_t r = 0; r < th; ++r)
        {
            int32_t row = ty + 1 + r;
            if (row < 0 || row >= (int32_t)s.viewportRows)
                continue;
            if (endCol > startCol)
                UiPaint::Text((uint32_t)startCol, (uint32_t)row, byteview{line + skip, (uint64_t)(endCol - startCol)},
                              s.theme.focusedFg, s.theme.bg);
        }
    }

    UiPaint::PushClip(slot->x, slot->y + 1, slot->w, slot->h);

    s.savedCursorX = s.cursorX;
    s.savedCursorY = s.cursorY;
    s.savedLineStartX = s.lineStartX;
    s.savedLineH = s.lineH;
    s.savedPointerSuppress = s.pointerSuppress;
    s.savedInModalScope = s.inModalScope;

    s.cursorX = slot->x;
    s.cursorY = slot->y + 1;
    s.lineStartX = slot->x;
    s.lineH = 0;
    // The topmost window owns its content's input layer regardless of root-scope modal
    // suppression. Non-topmost windows inherit suppress (modal- or above-window-). When a
    // modal is active, only the modal window's scope (its widgets) sees pointer events.
    bool isModalWindow = (desc.flags & kWindowFlagModal) != 0;
    s.pointerSuppress = !topmost || (s.modalActive && !isModalWindow);
    s.inModalScope = isModalWindow;
    s.openWindowId = id;
    slot->curItemsBegin = s.curCount;
    return true;
}

void EndWindow(ui_state &s)
{
    ASSERT(s.openWindowId != 0);
    ui_window_slot *slot = FindWindowSlot(s, s.openWindowId);
    ASSERT(slot != nullptr);
    slot->curItemsEnd = s.curCount;
    // Focus-on-open: a freshly opened window grabs focus to its first registered widget,
    // so keyboard nav lands inside the new content immediately.
    if (slot->openedJustNow && slot->curItemsEnd > slot->curItemsBegin)
        s.focusId = s.curItems[slot->curItemsBegin];
    UiPaint::PopClip();
    s.cursorX = s.savedCursorX;
    s.cursorY = s.savedCursorY;
    s.lineStartX = s.savedLineStartX;
    s.lineH = s.savedLineH;
    s.pointerSuppress = s.savedPointerSuppress;
    s.inModalScope = s.savedInModalScope;
    s.openWindowId = 0;
}

namespace
{

auto FindOrAllocListSlot(ui_state &s, uint32_t id) -> ui_list_slot *
{
    for (uint32_t i = 0; i < s.listCount; ++i)
        if (s.lists[i].id == id)
            return &s.lists[i];
    if (s.listCount >= ui_state::kListSlotsMax)
        return nullptr;
    ui_list_slot *l = &s.lists[s.listCount++];
    *l = ui_list_slot{};
    l->id = id;
    return l;
}

} // namespace

auto BeginList(ui_state &s, uint32_t localId, const ui_list_desc &desc) -> ui_list_scope
{
    uint32_t id = MakeId(s, localId);
    ui_list_slot *slot = FindOrAllocListSlot(s, id);
    if (slot)
        slot->presentThisFrame = true;

    // Map this frame's focus id back to a row index using the slot's prev-frame item range.
    // Begin set s.focusId by indexing prev items, so its position in our range == fIdx.
    // Row order must be stable across frames for this to be accurate; filter changes that
    // shuffle rows are the caller's responsibility (reset scroll on changes).
    uint32_t curFocusedFIdx = UINT32_MAX;
    if (slot && slot->prevItemsEnd > slot->prevItemsBegin)
    {
        for (uint32_t i = slot->prevItemsBegin; i < slot->prevItemsEnd; ++i)
        {
            if (s.prevItems[i] == s.focusId)
            {
                curFocusedFIdx = i - slot->prevItemsBegin;
                break;
            }
        }
    }

    if (desc.visibleRows > 0 && desc.scroll)
    {
        if (curFocusedFIdx != UINT32_MAX)
        {
            if (curFocusedFIdx < *desc.scroll)
                *desc.scroll = curFocusedFIdx;
            else if (curFocusedFIdx >= *desc.scroll + (uint32_t)desc.visibleRows)
                *desc.scroll = curFocusedFIdx - (uint32_t)desc.visibleRows + 1;
        }
        if (desc.rowCount <= (uint32_t)desc.visibleRows)
            *desc.scroll = 0;
        else if (*desc.scroll + (uint32_t)desc.visibleRows > desc.rowCount)
            *desc.scroll = desc.rowCount - (uint32_t)desc.visibleRows;
    }

    ui_list_scope scope{};
    scope.baseX = s.cursorX;
    scope.baseY = s.cursorY;
    scope.w = desc.w;
    scope.visibleRows = desc.visibleRows;
    scope.rowCount = desc.rowCount;
    scope.firstVisible = desc.scroll ? *desc.scroll : 0;
    scope.lastVisible = scope.firstVisible + (uint32_t)desc.visibleRows;
    if (scope.lastVisible > desc.rowCount)
        scope.lastVisible = desc.rowCount;
    scope.focusedFIdx = UINT32_MAX;
    scope.slotIdx = slot ? (uint32_t)(slot - s.lists) : UINT32_MAX;

    UiPaint::PushClip(scope.baseX, scope.baseY, desc.w, desc.visibleRows);
    if (slot)
        slot->curItemsBegin = s.curCount;

    return scope;
}

auto ListRow(ui_state &s, ui_list_scope &scope, uint32_t fIdx, uint32_t rowLocalId) -> ui_list_row_result
{
    ui_list_row_result r{};
    bool visible = fIdx >= scope.firstVisible && fIdx < scope.lastVisible;
    r.visible = visible;
    if (visible)
    {
        int32_t y = scope.baseY + (int32_t)(fIdx - scope.firstVisible);
        r.y = y;
        r.hit = SelectableHit(s, rowLocalId, scope.baseX, y, scope.w, 1);
    }
    else
    {
        r.hit = Selectable(s, rowLocalId);
    }
    if (r.hit.focused)
        scope.focusedFIdx = fIdx;
    return r;
}

void EndList(ui_state &s, const ui_list_scope &scope)
{
    UiPaint::PopClip();
    if (scope.slotIdx != UINT32_MAX)
        s.lists[scope.slotIdx].curItemsEnd = s.curCount;
    s.cursorX = scope.baseX;
    s.cursorY = scope.baseY + scope.visibleRows;
    s.lineStartX = scope.baseX;
    s.lineH = 0;
    s.lastItemX = scope.baseX;
    s.lastItemY = scope.baseY;
    s.lastItemW = scope.w;
    s.lastItemH = scope.visibleRows;
}

auto BeginChild(ui_state &s, uint32_t localId, const ui_child_desc &desc) -> bool
{
    // Save current cursor state; child will reset it. EndChild restores.
    s.childEndX = s.cursorX;
    s.childEndY = s.cursorY;
    int32_t savedLineStartX = s.lineStartX;
    int32_t savedLineH = s.lineH;

    int32_t cx = desc.w == 0 ? (int32_t)s.viewportCols - s.cursorX : desc.w;
    int32_t cy = desc.h == 0 ? (int32_t)s.viewportRows - s.cursorY : desc.h;
    if (cx < 0)
        cx = 0;
    if (cy < 0)
        cy = 0;

    // Resolve child origin — use current cursor position.
    int32_t ox = s.cursorX;
    int32_t oy = s.cursorY;

    // Borders consume 2 cells of space (1 left + 1 right, 1 top + 1 bottom).
    int32_t borderPad = (desc.flags & kChildFlagBorder) ? 1 : 0;

    // Compute visible content region.
    int32_t contentW = cx - 2 * borderPad;
    int32_t contentH = cy - 2 * borderPad;
    if (contentW < 0)
        contentW = 0;
    if (contentH < 0)
        contentH = 0;

    // Push clip for content area.
    UiPaint::PushClip(ox + borderPad, oy + borderPad, contentW, contentH);

    // Reset cursor into the content area.
    s.cursorX = ox + borderPad;
    s.cursorY = oy + borderPad;
    s.lineStartX = s.cursorX;
    s.lineH = 0;

    // Border (drawn after clip so it overlaps content edges).
    if (borderPad && cx > 1 && cy > 1)
    {
        uint32_t fg = s.theme.focusedFg;

        // Top/bottom rows
        uint8_t hLine[256];
        MemSet(hLine, 0xC4, Min((uint32_t)cx - 2, (uint32_t)sizeof(hLine)));
        int32_t hLen = Min(cx - 2, (int32_t)sizeof(hLine));
        if (hLen > 0)
        {
            if (oy >= 0 && oy < (int32_t)s.viewportRows)
                UiPaint::Text(ox + 1, oy, byteview{hLine, (uint64_t)hLen}, fg, s.theme.bg);
            int32_t botY = oy + cy - 1;
            if (botY >= 0 && botY < (int32_t)s.viewportRows)
                UiPaint::Text(ox + 1, botY, byteview{hLine, (uint64_t)hLen}, fg, s.theme.bg);
        }

        // Corners
        auto corner = [&](int32_t x, int32_t y, uint8_t ch) {
            if (y >= 0 && y < (int32_t)s.viewportRows && x >= 0 && x < (int32_t)s.viewportCols)
                UiPaint::Text(x, y, byteview{&ch, 1}, fg, s.theme.bg);
        };
        corner(ox, oy, 0xDA);                   // ┌
        corner(ox + cx - 1, oy, 0xBF);          // ┐
        corner(ox, oy + cy - 1, 0xC0);          // └
        corner(ox + cx - 1, oy + cy - 1, 0xD9); // ┘

        // Vertical lines
        for (int32_t r = 1; r < cy - 1; ++r)
        {
            int32_t row = oy + r;
            if (row >= 0 && row < (int32_t)s.viewportRows)
            {
                uint8_t v = 0xB3; // │
                UiPaint::Text(ox, row, byteview{&v, 1}, fg, s.theme.bg);
                UiPaint::Text(ox + cx - 1, row, byteview{&v, 1}, fg, s.theme.bg);
            }
        }
    }

    // Scroll handling: consume wheel edges when pointer is over child rect.
    if ((desc.flags & kChildFlagScrollable) && desc.scrollY && contentH > 0)
    {
        bool pointerOverChild = s.pointerX >= ox && s.pointerX < ox + cx && s.pointerY >= oy && s.pointerY < oy + cy;

        if (pointerOverChild)
        {
            if (s.wheelEdgeUp && *desc.scrollY > 0)
            {
                --*desc.scrollY;
                s.wheelEdgeUp = false;
            }
            if (s.wheelEdgeDn)
            {
                ++*desc.scrollY;
                s.wheelEdgeDn = false;
            }
        }

        // Clamp
        if (desc.contentRows <= (uint32_t)contentH)
            *desc.scrollY = 0;
        else if (*desc.scrollY + (uint32_t)contentH > desc.contentRows)
            *desc.scrollY = desc.contentRows - (uint32_t)contentH;
    }

    (void)localId;
    (void)savedLineStartX;
    (void)savedLineH;
    return true;
}

void EndChild(ui_state &s)
{
    UiPaint::PopClip();
    // Restore cursor to what it was before BeginChild.
    s.cursorX = s.childEndX;
    s.cursorY = s.childEndY;
    s.lineStartX = s.cursorX;
    s.lineH = 0;
}

} // namespace Ui

} // namespace nyla
