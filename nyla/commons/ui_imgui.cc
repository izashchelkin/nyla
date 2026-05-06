#include "nyla/commons/ui_imgui.h"

#include "nyla/commons/cell_renderer.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/hash.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/tunables.h"

namespace nyla::Ui
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
        CellRenderer::Text((uint32_t)x, (uint32_t)y, byteview{text.data, (uint64_t)printable}, fg, s.theme.bg);
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

auto SelectableHit(ui_state &s, uint32_t localId, int32_t x, int32_t y, int32_t w, int32_t h)
    -> ui_selectable_result
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

auto Button(ui_state &s, uint32_t localId, byteview label, uint32_t restingFg) -> bool
{
    uint32_t id = MakeId(s, localId);
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
        line[0] = '[';
        line[1] = ' ';
        uint64_t copy = label.size;
        if (copy > sizeof(line) - 4)
            copy = sizeof(line) - 4;
        MemCpy(line + 2, label.data, copy);
        line[2 + copy] = ' ';
        line[3 + copy] = ']';
        total = (uint32_t)(4 + copy);
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
        CellRenderer::Text((uint32_t)x, (uint32_t)y, byteview{line, total}, fg, bg);
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
        CellRenderer::Text((uint32_t)x, (uint32_t)y, byteview{line, printable}, fg, s.theme.bg);
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
        uint64_t n =
            StringWriteFmt(span<uint8_t>{namebuf, sizeof(namebuf)}, "ui.win.%08x.x"_s, id);
        Tunables::RegisterInt(byteview{namebuf, n}, &slot->x, 1, -10000, 10000);
        n = StringWriteFmt(span<uint8_t>{namebuf, sizeof(namebuf)}, "ui.win.%08x.y"_s, id);
        Tunables::RegisterInt(byteview{namebuf, n}, &slot->y, 1, -10000, 10000);
    }

    slot->flags = desc.flags;
    slot->w = desc.w;
    slot->h = desc.h;
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
            CellRenderer::Text((uint32_t)startCol, (uint32_t)ty,
                               byteview{line + skip, (uint64_t)(endCol - startCol)},
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
                CellRenderer::Text((uint32_t)startCol, (uint32_t)row,
                                   byteview{line + skip, (uint64_t)(endCol - startCol)},
                                   s.theme.focusedFg, s.theme.bg);
        }
    }

    CellRenderer::PushClip(slot->x, slot->y + 1, slot->w, slot->h);

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
    CellRenderer::PopClip();
    s.cursorX = s.savedCursorX;
    s.cursorY = s.savedCursorY;
    s.lineStartX = s.savedLineStartX;
    s.lineH = s.savedLineH;
    s.pointerSuppress = s.savedPointerSuppress;
    s.inModalScope = s.savedInModalScope;
    s.openWindowId = 0;
}

} // namespace nyla::Ui
