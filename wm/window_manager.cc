#include <unistd.h>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/binary_search.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/platform_linux.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/shm_channel.h"
#include "nyla/commons/span.h" // IWYU pragma: keep
#include "nyla/commons/time.h"
#include "nyla/commons/wm_ipc.h"
#include "nyla/commons/word.h"
#include "nyla/commons/x11_wm_hints_linux.h"

namespace nyla
{

namespace
{

struct Rect
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(Rect) == 16);

enum class Color : uint32_t
{
    KNone = 0x000000,
    KActive = 0x95A3B3,
    KActiveFollow = 0x84DCC6,
};

struct window_stack
{
    uint16_t flags;
    xcb_window_t activeWindow;
    int32_t scrollOffset;
    inline_vec<xcb_window_t, 64> windows;

    static inline constexpr decltype(flags) FlagZoom = 1 << 0;
    static inline constexpr decltype(flags) FlagCenterScroll = 1 << 1;
};

enum net_wm_moveresize_direction : uint32_t
{
    MoveresizeSizeTopLeft = 0,
    MoveresizeSizeTop = 1,
    MoveresizeSizeTopRight = 2,
    MoveresizeSizeRight = 3,
    MoveresizeSizeBottomRight = 4,
    MoveresizeSizeBottom = 5,
    MoveresizeSizeBottomLeft = 6,
    MoveresizeSizeLeft = 7,
    MoveresizeMove = 8,
    MoveresizeSizeKeyboard = 9,
    MoveresizeMoveKeyboard = 10,
    MoveresizeCancel = 11,
};

struct window_pending_property
{
    xcb_atom_t atom;
    xcb_get_property_cookie_t cookie;
};

struct window_data_entry
{
    xcb_window_t window;
    uint32_t borderWidth;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint8_t tierIndex;
    Rect rect;
    inline_string<64> name;
    inline_vec<window_pending_property, 8> pendingCookies;
    xcb_get_window_attributes_cookie_t pendingAttrCookie{};
};

struct window_index_entry
{
    xcb_window_t window;
    xcb_window_t parent;
    uint16_t flags;
    window_data_entry *dataEntry;

    static inline constexpr decltype(flags) Flag_WM_Hints_Input = 1 << 0;
    static inline constexpr decltype(flags) Flag_WM_TakeFocus = 1 << 1;
    static inline constexpr decltype(flags) Flag_WantsConfigureNotify = 1 << 4;
    static inline constexpr decltype(flags) Flag_PendingParent = 1 << 5;
};

// Maps window XID → saved stack/position for WM restart recovery.
struct restore_entry
{
    xcb_window_t window;
    uint8_t stackIndex;
    uint8_t position; // index within the stack's windows list
    uint8_t tierIndex;
};

static constexpr uint32_t kTierCount = 4;

struct window_manager
{
    uint8_t activeStackIndex;
    bool layoutDirty;
    bool borderDirty;
    bool follow;
    bool focusCheckPending;
    bool restoreHasData; // true until deferred restore completes

    uint32_t barHeight;
    xcb_window_t lastEnteredWindow;
    xcb_get_input_focus_cookie_t focusCheckCookie;

    bool moveresizeActive;
    xcb_window_t moveresizeWindow;
    int32_t moveresizeOrigPointerX;
    uint32_t moveresizeOrigWidth;
    net_wm_moveresize_direction moveresizeDirection;

    array<window_stack, 9> stacks;
    inline_vec<xcb_window_t, 64> pendingClients;
    inline_vec<restore_entry, 576> restoreMap;
    xcb_window_t savedActiveXids[9];
    uint8_t restoreActiveStackIndex;
    array<uint32_t, kTierCount> tiers;

    span<uint8_t> memory;
    uint32_t capacity;
    uint32_t windowCount;
    window_index_entry *index;
    window_data_entry *data;

    shm_channel *ipcChannel = nullptr;
};

window_manager *wm;
static uint64_t sIpcGeneration;

static constexpr uint32_t kWmEventMask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE |
                                         XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;

// -- index helpers --

auto FindIndex(xcb_window_t w) -> window_index_entry *
{
    return BinarySearch::Find(span<window_index_entry>{wm->index, wm->windowCount}, w,
                              [](const window_index_entry &e) { return e.window; });
}

// -- stack helpers --

auto GetActiveStack() -> window_stack &
{
    return wm->stacks[wm->activeStackIndex];
}

auto FindRestorePos(xcb_window_t w) -> restore_entry *
{
    for (uint64_t i = 0; i < wm->restoreMap.size; ++i)
    {
        if (wm->restoreMap[i].window == w)
            return &wm->restoreMap[i];
    }
    return nullptr;
}

// -- core operations --

void ApplyBorder(xcb_window_t window, Color color)
{
    if (!window)
        return;
    xcb_change_window_attributes(X11GetConn(), window, XCB_CW_BORDER_PIXEL, &color);
}

void ConfigureClientIfNeeded(xcb_window_t clientWindow, window_index_entry &idx, window_data_entry &data,
                             const Rect &newRect, uint32_t newBorderWidth)
{
    uint16_t mask = 0;
    inline_vec<uint32_t, 5> values{};
    bool anythingChanged = false;
    bool sizeChanged = false;

    if (newRect.x != data.rect.x)
    {
        anythingChanged = true;
        mask |= XCB_CONFIG_WINDOW_X;
        InlineVec::Append(values, static_cast<uint32_t>(newRect.x));
    }
    if (newRect.y != data.rect.y)
    {
        anythingChanged = true;
        mask |= XCB_CONFIG_WINDOW_Y;
        InlineVec::Append(values, static_cast<uint32_t>(newRect.y));
    }
    if (newRect.width != data.rect.width)
    {
        anythingChanged = true;
        sizeChanged = true;
        mask |= XCB_CONFIG_WINDOW_WIDTH;
        InlineVec::Append(values, newRect.width);
    }
    if (newRect.height != data.rect.height)
    {
        anythingChanged = true;
        sizeChanged = true;
        mask |= XCB_CONFIG_WINDOW_HEIGHT;
        InlineVec::Append(values, newRect.height);
    }
    if (newBorderWidth != data.borderWidth)
    {
        anythingChanged = true;
        sizeChanged = true;
        mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        InlineVec::Append(values, newBorderWidth);
    }

    if (anythingChanged)
    {
        xcb_configure_window(X11GetConn(), clientWindow, mask, InlineVec::DataPtr(values));

        if (sizeChanged)
            idx.flags &= ~window_index_entry::Flag_WantsConfigureNotify;
        else
            idx.flags |= window_index_entry::Flag_WantsConfigureNotify;

        data.rect = newRect;
        data.borderWidth = newBorderWidth;
    }
}

void FetchClientProperty(xcb_window_t clientWindow, window_data_entry &data, xcb_atom_t property)
{
    switch (property)
    {
    case XCB_ATOM_WM_HINTS:
    case XCB_ATOM_WM_NORMAL_HINTS:
    case XCB_ATOM_WM_NAME:
    case XCB_ATOM_WM_TRANSIENT_FOR:
        break;
    default:
        if (property == X11GetAtoms().wm_protocols)
            break;
        return;
    }

    for (uint64_t i = 0; i < data.pendingCookies.size; ++i)
        if (data.pendingCookies[i].atom == property)
            return;

    xcb_get_property_cookie_t cookie =
        xcb_get_property_unchecked(X11GetConn(), 0, clientWindow, property, XCB_ATOM_ANY, 0, UINT32_MAX);
    window_pending_property &pp = InlineVec::Append(data.pendingCookies);
    pp = {property, cookie};
}

static constexpr uint32_t kDefaultWindowWidth = 1280;
static constexpr const char *kStateFilePath = "/tmp/nyla_wm_state";
static constexpr uint32_t kStateMagic = DWordBE("NYLA");

void ManageClient(xcb_window_t clientWindow)
{
    if (FindIndex(clientWindow))
        return;

    if (wm->windowCount >= wm->capacity)
    {
        uint32_t newCap = wm->capacity * 2;
        window_data_entry *newData = (window_data_entry *)(wm->index + newCap);
        CommitMemPages(wm->memory.data, (uint8_t *)(newData + newCap) - wm->memory.data);
        window_data_entry *oldData = wm->data;
        MemMove(newData, oldData, wm->windowCount * sizeof(window_data_entry));
        wm->capacity = newCap;
        wm->data = newData;
        for (uint32_t i = 0; i < wm->windowCount; ++i)
            wm->index[i].dataEntry = wm->data + (wm->index[i].dataEntry - oldData);
    }

    uint32_t i = wm->windowCount++;
    MemZero(&wm->data[i]);
    wm->data[i].window = clientWindow;
    wm->data[i].tierIndex = 1;
    uint64_t pos = BinarySearch::LowerBound(span<window_index_entry>{wm->index, (uint64_t)i}, clientWindow,
                                            [](const window_index_entry &e) { return e.window; });
    MemMove(&wm->index[pos + 1], &wm->index[pos], (i - pos) * sizeof(window_index_entry));
    MemZero(&wm->index[pos]);
    wm->index[pos].window = clientWindow;
    wm->index[pos].dataEntry = &wm->data[i];

    xcb_change_window_attributes(X11GetConn(), clientWindow, XCB_CW_EVENT_MASK, &kWmEventMask);

    window_data_entry &data = wm->data[i];
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_HINTS);
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_NORMAL_HINTS);
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_NAME);
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_TRANSIENT_FOR);
    FetchClientProperty(clientWindow, data, X11GetAtoms().wm_protocols);

    data.pendingAttrCookie = xcb_get_window_attributes(X11GetConn(), clientWindow);
    X11Flush(); // send attr request now so reply is ready by deferred check

    InlineVec::Append(wm->pendingClients, clientWindow);
}

void Activate(const window_stack &stack, xcb_timestamp_t time)
{
    auto revertToRoot = [&] {
        xcb_set_input_focus(X11GetConn(), XCB_INPUT_FOCUS_NONE, X11GetRoot(), time);
        wm->lastEnteredWindow = 0;
    };

    if (!stack.activeWindow)
    {
        revertToRoot();
        return;
    }

    window_index_entry *idx = FindIndex(stack.activeWindow);
    if (!idx)
    {
        revertToRoot();
        return;
    }

    wm->borderDirty = true;

    xcb_window_t immediateFocus;
    if (idx->flags & window_index_entry::Flag_WM_Hints_Input)
        immediateFocus = stack.activeWindow;
    else
        immediateFocus = X11GetRoot();
    xcb_set_input_focus(X11GetConn(), XCB_INPUT_FOCUS_NONE, immediateFocus, time);

    if (idx->flags & window_index_entry::Flag_WM_TakeFocus)
        X11SendWmTakeFocus(stack.activeWindow, time);
}

void Activate(window_stack &stack, xcb_window_t clientWindow, xcb_timestamp_t time)
{
    if (stack.activeWindow != clientWindow)
    {
        ApplyBorder(stack.activeWindow, Color::KNone);
        stack.activeWindow = clientWindow;
        wm->layoutDirty = true;
    }
    Activate(stack, time);
}

void ClearZoom(window_stack &stack)
{
    if (!(stack.flags & window_stack::FlagZoom))
        return;
    stack.flags &= ~window_stack::FlagZoom;
    wm->layoutDirty = true;
}

void UnmanageClient(xcb_window_t clientWindow)
{
    window_index_entry *idx = FindIndex(clientWindow);
    if (!idx)
        return;

    window_data_entry &data = *idx->dataEntry;

    for (uint64_t j = 0; j < data.pendingCookies.size; ++j)
        xcb_discard_reply(X11GetConn(), data.pendingCookies[j].cookie.sequence);
    if (data.pendingAttrCookie.sequence)
        xcb_discard_reply(X11GetConn(), data.pendingAttrCookie.sequence);

    for (uint32_t i = 0; i < wm->windowCount; ++i)
    {
        window_index_entry &orphan = wm->index[i];
        if ((orphan.flags & window_index_entry::Flag_PendingParent) && orphan.parent == clientWindow)
        {
            orphan.flags &= ~window_index_entry::Flag_PendingParent;
            orphan.parent = 0;
        }
    }

    {
        span<window_index_entry> rws{wm->index, wm->windowCount};
        uint32_t rwIdxPos = (uint32_t)(idx - wm->index);
        uint32_t rwDataPos = (uint32_t)(idx->dataEntry - wm->data);
        uint32_t rwLast = wm->windowCount - 1;
        if (rwDataPos != rwLast)
        {
            wm->data[rwDataPos] = wm->data[rwLast];
            window_index_entry *movedIdx = BinarySearch::Find(rws, wm->data[rwDataPos].window,
                                                              [](const window_index_entry &e) { return e.window; });
            ASSERT(movedIdx);
            movedIdx->dataEntry = &wm->data[rwDataPos];
        }
        MemMove(&wm->index[rwIdxPos], &wm->index[rwIdxPos + 1], (rwLast - rwIdxPos) * sizeof(window_index_entry));
        --wm->windowCount;
    }

    for (uint32_t istack = 0; istack < 9; ++istack)
    {
        window_stack &stack = wm->stacks[istack];
        xcb_window_t *winPos = InlineVec::Find(stack.windows, clientWindow);
        if (!winPos)
            continue;

        wm->layoutDirty = true;
        wm->follow = false;
        stack.flags &= ~window_stack::FlagZoom;
        InlineVec::Erase(stack.windows, winPos);

        if (stack.activeWindow == clientWindow)
        {
            stack.activeWindow = 0;
            if (istack == wm->activeStackIndex)
            {
                xcb_window_t fallback = 0;
                if (stack.windows.size > 0)
                {
                    uint64_t widx = (uint64_t)(winPos - InlineVec::DataPtr(stack.windows));
                    fallback = stack.windows[widx > 0 ? widx - 1 : 0];
                }
                Activate(stack, fallback, XCB_CURRENT_TIME);
            }
        }
        return;
    }
}

// -- actions --

void MoveStack(xcb_timestamp_t time, auto computeIdx)
{
    uint32_t iold = wm->activeStackIndex;
    uint32_t inew = (uint32_t)(computeIdx((uint64_t)iold + 9) % 9);
    if (iold == inew)
        return;

    window_stack &oldStack = wm->stacks[iold];
    wm->activeStackIndex = (uint8_t)inew;
    window_stack &newStack = wm->stacks[inew];

    if (wm->follow)
    {
        if (oldStack.activeWindow)
        {
            newStack.activeWindow = oldStack.activeWindow;
            InlineVec::Append(newStack.windows, oldStack.activeWindow);
            newStack.flags &= ~window_stack::FlagZoom;
            oldStack.flags &= ~window_stack::FlagZoom;

            xcb_window_t *pos = InlineVec::Find(oldStack.windows, oldStack.activeWindow);
            ASSERT(pos);
            InlineVec::Erase(oldStack.windows, pos);

            if (oldStack.windows.size > 0)
                oldStack.activeWindow = oldStack.windows[0];
            else
                oldStack.activeWindow = 0;
            Activate(newStack, newStack.activeWindow, time);
            wm->borderDirty = true;
        }
    }
    else
    {
        ApplyBorder(oldStack.activeWindow, Color::KNone);
        Activate(newStack, newStack.activeWindow, time);
    }

    wm->layoutDirty = true;
}

void MoveStackNext(xcb_timestamp_t time)
{
    MoveStack(time, [](uint64_t idx) { return idx + 1; });
}

void MoveStackPrev(xcb_timestamp_t time)
{
    MoveStack(time, [](uint64_t idx) { return idx - 1; });
}

void MoveLocal(xcb_timestamp_t time, auto computeIdx, bool clearZoom)
{
    window_stack &stack = GetActiveStack();
    if (clearZoom)
        ClearZoom(stack);

    if (stack.windows.size == 0)
        return;
    if (stack.activeWindow && stack.windows.size < 2)
        return;

    if (stack.activeWindow)
    {
        xcb_window_t *pos = InlineVec::Find(stack.windows, stack.activeWindow);
        if (!pos)
            return;

        uint64_t iold = (uint64_t)(pos - InlineVec::DataPtr(stack.windows));
        uint64_t inew = computeIdx(iold + stack.windows.size) % stack.windows.size;
        if (iold == inew)
            return;

        if (wm->follow)
        {
            Swap(stack.windows[iold], stack.windows[inew]);
            wm->layoutDirty = true;
        }
        else
        {
            Activate(stack, stack.windows[inew], time);
        }
    }
    else
    {
        if (!wm->follow)
            Activate(stack, stack.windows[0], time);
    }
}

void MoveLocalNext(xcb_timestamp_t time, bool clearZoom)
{
    MoveLocal(time, [](uint64_t idx) { return idx + 1; }, clearZoom);
}

void MoveLocalPrev(xcb_timestamp_t time, bool clearZoom)
{
    MoveLocal(time, [](uint64_t idx) { return idx - 1; }, clearZoom);
}

// Recompute wm->tiers from current viewport width.
void RefreshTiers()
{
    uint32_t viewW = X11GetMonitorWidth();
    uint32_t base = kDefaultWindowWidth;
    uint32_t n = 0;
    wm->tiers[n++] = Max(base - base / 4, 320u);
    wm->tiers[n++] = base;
    uint32_t mid = Min(base + base / 2, viewW);
    if (mid < viewW * 4 / 5)
        wm->tiers[n++] = mid;
    uint32_t fullW = viewW > 40 ? viewW - 40 : viewW;
    if (wm->tiers[n - 1] != fullW)
        wm->tiers[n++] = fullW;
    for (uint32_t i = n; i < kTierCount; ++i)
        wm->tiers[i] = wm->tiers[n - 1];
}

auto TierWidth(uint32_t idx) -> uint32_t
{
    return wm->tiers[idx];
}

auto SnapToNearestTierIdx(uint32_t width) -> uint8_t
{
    uint8_t best = 0;
    uint32_t bestD = width < wm->tiers[0] ? wm->tiers[0] - width : width - wm->tiers[0];
    for (uint32_t i = 1; i < kTierCount; ++i)
    {
        uint32_t d = width < wm->tiers[i] ? wm->tiers[i] - width : width - wm->tiers[i];
        if (d < bestD)
        {
            bestD = d;
            best = (uint8_t)i;
        }
    }
    return best;
}

void ResizeActive(int32_t direction)
{
    window_stack &stack = GetActiveStack();
    if (!stack.activeWindow)
        return;
    window_index_entry *idx = FindIndex(stack.activeWindow);
    if (!idx)
        return;
    int32_t next = (int32_t)idx->dataEntry->tierIndex + direction;
    while (next >= 0 && next < (int32_t)kTierCount && wm->tiers[(uint32_t)next] == wm->tiers[idx->dataEntry->tierIndex])
        next += direction;
    if (next < 0 || next >= (int32_t)kTierCount)
        return;
    idx->dataEntry->tierIndex = (uint8_t)next;
    wm->layoutDirty = true;
}

void WmProcess(bool &isRunning)
{
    while (isRunning)
    {
        xcb_generic_event_t *event = xcb_poll_for_event(X11GetConn());
        if (!event)
            break;

        uint8_t eventType = event->response_type & 0x7F;
        window_stack &stack = GetActiveStack();

        switch (eventType)
        {
        case XCB_KEY_PRESS: {
            auto *kp = reinterpret_cast<xcb_key_press_event_t *>(event);
            const bool meta = kp->state & XCB_MOD_MASK_4;
            const bool alt = kp->state & XCB_MOD_MASK_1;
            const bool ctrl = kp->state & XCB_MOD_MASK_CONTROL;
            const bool shift = kp->state & XCB_MOD_MASK_SHIFT;

            KeyPhysical key;
            if (!X11KeyCodeToKeyPhysical(kp->detail, &key))
                break;

            if (meta && ctrl && key == KeyPhysical::ArrowLeft)
            {
                MoveStackPrev(kp->time);
                break;
            }

            if (meta && ctrl && key == KeyPhysical::ArrowRight)
            {
                MoveStackNext(kp->time);
                break;
            }

            if (meta && key == KeyPhysical::E)
            {
                MoveStackPrev(kp->time);
                break;
            }

            if (meta && shift && key == KeyPhysical::R)
            {
                if (wm->moveresizeActive)
                {
                    xcb_ungrab_pointer(X11GetConn(), XCB_CURRENT_TIME);
                    wm->moveresizeActive = false;
                    wm->moveresizeWindow = XCB_NONE;
                }
                {
                    uint8_t buf[4096];
                    uint32_t pos = 0;
                    auto writeU32 = [&](uint32_t v) {
                        MemCpy(buf + pos, &v, 4);
                        pos += 4;
                    };
                    auto writeU8 = [&](uint8_t v) { buf[pos++] = v; };
                    writeU32(kStateMagic);
                    writeU8(wm->activeStackIndex);
                    writeU8(wm->follow ? 1 : 0);
                    for (int s = 0; s < 9; ++s)
                    {
                        const window_stack &stk = wm->stacks[s];
                        writeU8(0);
                        writeU8((uint8_t)(stk.flags & 0xFF));
                        writeU8((uint8_t)(stk.flags >> 8));
                        uint8_t winCount = (uint8_t)stk.windows.size;
                        writeU8(winCount);
                        for (uint64_t j = 0; j < stk.windows.size; ++j)
                            writeU32((uint32_t)stk.windows[j]);
                        for (uint64_t j = 0; j < stk.windows.size; ++j)
                        {
                            window_index_entry *ix = FindIndex(stk.windows[j]);
                            writeU32(ix ? ix->dataEntry->tierIndex : 1);
                        }
                        writeU32((uint32_t)stk.activeWindow);
                    }
                    file_handle f =
                        FileOpen({(uint8_t *)kStateFilePath, CStrLen(kStateFilePath, 32)}, FileOpenMode::Write);
                    if (FileValid(f))
                    {
                        FileWrite(f, pos, buf);
                        FileClose(f);
                    }
                }
                X11Ungrab();
                xcb_disconnect(X11GetConn());
                if (wm->ipcChannel)
                {
                    ShmChannel::Close(*wm->ipcChannel);
                    wm->ipcChannel = nullptr;
                }
                isRunning = false;
                break;
            }

            if (meta && key == KeyPhysical::R)
            {
                MoveStackNext(kp->time);
                break;
            }

            if (alt && shift && key == KeyPhysical::Tab)
            {
                MoveLocalPrev(kp->time, false);
                break;
            }

            if (alt && key == KeyPhysical::Tab)
            {
                MoveLocalNext(kp->time, false);
                break;
            }

            if (meta && key == KeyPhysical::D)
            {
                MoveLocalPrev(kp->time, true);
                break;
            }

            if (meta && key == KeyPhysical::F)
            {
                MoveLocalNext(kp->time, true);
                break;
            }

            if (meta && key == KeyPhysical::G)
            {
                GetActiveStack().flags ^= window_stack::FlagZoom;
                wm->layoutDirty = true;
                wm->borderDirty = true;
                break;
            }

            if (meta && key == KeyPhysical::V)
            {
                window_stack &tfs = GetActiveStack();
                if (!tfs.activeWindow)
                {
                    wm->follow = false;
                }
                else
                {
                    window_index_entry *tfidx = FindIndex(tfs.activeWindow);
                    if (!tfidx || tfidx->parent)
                        wm->follow = false;
                    else
                    {
                        wm->follow = !wm->follow;
                        if (!wm->follow)
                            ClearZoom(tfs);
                    }
                }
                wm->borderDirty = true;
                break;
            }

            if (meta && key == KeyPhysical::ArrowLeft)
            {
                ResizeActive(-1);
                break;
            }

            if (meta && key == KeyPhysical::ArrowRight)
            {
                ResizeActive(1);
                break;
            }

            if (meta && key == KeyPhysical::Backspace)
            {
                window_stack &tcs = GetActiveStack();
                if (tcs.activeWindow)
                {
                    tcs.flags ^= window_stack::FlagCenterScroll;
                    wm->layoutDirty = true;
                }
                break;
            }

            if (alt && key == KeyPhysical::F4)
            {
                window_stack &cas = GetActiveStack();
                if (cas.activeWindow)
                {
                    static uint64_t lastClose = 0;
                    uint64_t nowClose = GetMonotonicTimeMillis();
                    if (nowClose - lastClose >= 100)
                        X11SendWmDeleteWindow(cas.activeWindow);
                    lastClose = nowClose;
                }
                break;
            }

            if (meta && key == KeyPhysical::T)
            {
                const char *const ghostty[] = {"ghostty", nullptr};
                Spawn({ghostty, 2});
                break;
            }

            if (meta && key == KeyPhysical::S)
            {
                const char *const dmenu[] = {"dmenu_run", nullptr};
                Spawn({dmenu, 2});
                break;
            }
            break;
        }

        case XCB_CLIENT_MESSAGE: {
            auto *cm = reinterpret_cast<xcb_client_message_event_t *>(event);
            if (cm->type == X11GetAtoms().net_wm_moveresize)
            {
                if (!wm->moveresizeActive)
                {
                    xcb_window_t clientWin = cm->window;
                    window_index_entry *idx = FindIndex(clientWin);
                    if (idx)
                    {
                        int32_t direction = cm->data.data32[2];
                        if (direction == MoveresizeCancel)
                            break;

                        // Only handle horizontal resize edges
                        if (direction == MoveresizeSizeRight || direction == MoveresizeSizeLeft ||
                            direction == MoveresizeSizeTopRight || direction == MoveresizeSizeBottomRight ||
                            direction == MoveresizeSizeTopLeft || direction == MoveresizeSizeBottomLeft)
                        {
                            wm->moveresizeActive = true;
                            wm->moveresizeWindow = clientWin;
                            wm->moveresizeDirection = static_cast<net_wm_moveresize_direction>(direction);
                            wm->moveresizeOrigWidth = TierWidth(idx->dataEntry->tierIndex);
                            wm->moveresizeOrigPointerX = cm->data.data32[0];

                            // Grab pointer — motion/button events go to WM
                            xcb_grab_pointer_cookie_t grabCookie = xcb_grab_pointer(
                                X11GetConn(), false, X11GetRoot(),
                                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                                XCB_GRAB_MODE_ASYNC, X11GetRoot(), XCB_NONE, XCB_CURRENT_TIME);
                            auto *grabReply = xcb_grab_pointer_reply(X11GetConn(), grabCookie, nullptr);
                            if (grabReply && grabReply->status != XCB_GRAB_STATUS_SUCCESS)
                                wm->moveresizeActive = false;
                            free(grabReply);
                        }
                    }
                }
                break;
            }
            // Fall through to property notify for other client messages
            [[fallthrough]];
        }

        case XCB_PROPERTY_NOTIFY: {
            auto *pn = reinterpret_cast<xcb_property_notify_event_t *>(event);
            window_index_entry *idx = FindIndex(pn->window);
            if (idx)
                FetchClientProperty(pn->window, *idx->dataEntry, pn->atom);
            break;
        }

        case XCB_CONFIGURE_REQUEST: {
            auto *cr = reinterpret_cast<xcb_configure_request_event_t *>(event);
            window_index_entry *idx = FindIndex(cr->window);
            if (idx)
            {
                // For managed windows: request a synthetic ConfigureNotify at frame end.
                // We don't honor resize-from-border requests (this is a tiling WM),
                // but the client must get a reply or it blocks forever during interactive resize.
                idx->flags |= window_index_entry::Flag_WantsConfigureNotify;
            }
            else
            {
                inline_vec<uint32_t, 7> values{};
                uint16_t mask = cr->value_mask;
                if (mask & XCB_CONFIG_WINDOW_X)
                    InlineVec::Append(values, (uint32_t)cr->x);
                if (mask & XCB_CONFIG_WINDOW_Y)
                    InlineVec::Append(values, (uint32_t)cr->y);
                if (mask & XCB_CONFIG_WINDOW_WIDTH)
                    InlineVec::Append(values, (uint32_t)cr->width);
                if (mask & XCB_CONFIG_WINDOW_HEIGHT)
                    InlineVec::Append(values, (uint32_t)cr->height);
                if (mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
                    InlineVec::Append(values, (uint32_t)cr->border_width);
                if (mask & XCB_CONFIG_WINDOW_SIBLING)
                    InlineVec::Append(values, (uint32_t)cr->sibling);
                if (mask & XCB_CONFIG_WINDOW_STACK_MODE)
                    InlineVec::Append(values, (uint32_t)cr->stack_mode);
                xcb_configure_window(X11GetConn(), cr->window, mask, InlineVec::DataPtr(values));
            }
            break;
        }

        case XCB_MAP_REQUEST: {
            xcb_map_window(X11GetConn(), reinterpret_cast<xcb_map_request_event_t *>(event)->window);
            break;
        }

        case XCB_MAP_NOTIFY: {
            auto *mn = reinterpret_cast<xcb_map_notify_event_t *>(event);
            if (!mn->override_redirect)
                ManageClient(mn->window);
            break;
        }

        case XCB_MAPPING_NOTIFY: {
            X11RefreshKeyboardMapping();
            break;
        }

        case XCB_UNMAP_NOTIFY: {
            UnmanageClient(reinterpret_cast<xcb_unmap_notify_event_t *>(event)->window);
            break;
        }

        case XCB_DESTROY_NOTIFY: {
            UnmanageClient(reinterpret_cast<xcb_destroy_notify_event_t *>(event)->window);
            break;
        }

        case XCB_FOCUS_IN: {
            auto *fi = reinterpret_cast<xcb_focus_in_event_t *>(event);
            if (fi->mode == XCB_NOTIFY_MODE_NORMAL && !wm->focusCheckPending)
            {
                wm->focusCheckCookie = xcb_get_input_focus(X11GetConn());
                wm->focusCheckPending = true;
            }
            break;
        }

        case XCB_GE_GENERIC: {
            auto *ge = reinterpret_cast<xcb_ge_generic_event_t *>(event);
            if (ge->extension == X11GetXInputExtensionMajorOpCode())
            {
                if (ge->event_type == XCB_INPUT_RAW_BUTTON_PRESS)
                {
                    auto *rb = reinterpret_cast<xcb_input_raw_button_press_event_t *>(event);
                    if (rb->detail == XCB_BUTTON_INDEX_1)
                    {
                        if (!(stack.flags & window_stack::FlagZoom) && !wm->follow && wm->lastEnteredWindow &&
                            wm->lastEnteredWindow != X11GetRoot() && wm->lastEnteredWindow != stack.activeWindow &&
                            FindIndex(wm->lastEnteredWindow))
                            Activate(stack, wm->lastEnteredWindow, rb->time);
                    }
                }
            }
            break;
        }

        case XCB_MOTION_NOTIFY: {
            if (wm->moveresizeActive)
            {
                auto *mn = reinterpret_cast<xcb_motion_notify_event_t *>(event);
                int32_t delta = (int32_t)(mn->root_x) - wm->moveresizeOrigPointerX;
                bool fromLeft = wm->moveresizeDirection == MoveresizeSizeLeft ||
                                wm->moveresizeDirection == MoveresizeSizeTopLeft ||
                                wm->moveresizeDirection == MoveresizeSizeBottomLeft;
                if (fromLeft)
                    delta = -delta;

                int32_t raw = (int32_t)wm->moveresizeOrigWidth + delta;
                uint8_t newIdx = SnapToNearestTierIdx((uint32_t)Clamp(raw, 320, 3840));
                window_index_entry *idx = FindIndex(wm->moveresizeWindow);
                if (idx && newIdx != idx->dataEntry->tierIndex)
                {
                    idx->dataEntry->tierIndex = newIdx;
                    wm->layoutDirty = true;
                }
                break;
            }
            break;
        }

        case XCB_BUTTON_RELEASE: {
            if (wm->moveresizeActive)
            {
                wm->moveresizeActive = false;
                wm->moveresizeWindow = XCB_NONE;
                xcb_ungrab_pointer(X11GetConn(), XCB_CURRENT_TIME);
            }
            break;
        }

        case XCB_ENTER_NOTIFY: {
            wm->lastEnteredWindow = reinterpret_cast<xcb_enter_notify_event_t *>(event)->event;
            break;
        }

        case 0: {
            auto *err = reinterpret_cast<xcb_generic_error_t *>(event);
            LOG("xcb error: %d, sequence: %d", err->error_code, err->sequence);
            break;
        }

        default: {
            uint32_t randrBase = X11RandRGetEventOffset();
            if (randrBase && (eventType == randrBase || eventType == randrBase + 1))
            {
                if (X11RandRRefreshMonitors())
                {
                    RefreshTiers();
                    wm->layoutDirty = true;
                }
            }
            break;
        }
        }

        free(event);
    }

    if (!isRunning)
        return;

    // Deferred focus theft check
    if (wm->focusCheckPending)
    {
        auto *r = xcb_get_input_focus_reply(X11GetConn(), wm->focusCheckCookie, nullptr);
        if (r)
        {
            {
                xcb_window_t focusedWindow = r->focus;
                const window_stack &ftStack = GetActiveStack();
                if (ftStack.activeWindow != focusedWindow && focusedWindow != X11GetRoot() && focusedWindow)
                {
                    if (!FindIndex(focusedWindow))
                    {
                        for (;;)
                        {
                            xcb_query_tree_reply_t *qr = xcb_query_tree_reply(
                                X11GetConn(), xcb_query_tree(X11GetConn(), focusedWindow), nullptr);
                            if (!qr)
                            {
                                Activate(ftStack, XCB_CURRENT_TIME);
                                goto focus_check_done;
                            }
                            xcb_window_t par = qr->parent;
                            free(qr);
                            if (!par || par == X11GetRoot())
                                break;
                            focusedWindow = par;
                        }
                    }
                    if (ftStack.activeWindow != focusedWindow)
                    {
                        window_index_entry *idx = FindIndex(focusedWindow);
                        if (!idx)
                        {
                            Activate(ftStack, XCB_CURRENT_TIME);
                        }
                        else if (idx->parent)
                        {
                            if (idx->parent != ftStack.activeWindow)
                            {
                                window_index_entry *activeIdx = FindIndex(ftStack.activeWindow);
                                if (!activeIdx || idx->parent != activeIdx->parent)
                                    Activate(ftStack, XCB_CURRENT_TIME);
                            }
                        }
                        else
                        {
                            Activate(ftStack, XCB_CURRENT_TIME);
                        }
                    }
                }
            focus_check_done:;
            }
            free(r);
        }
        wm->focusCheckPending = false;
    }

    // Dispatch pending property replies
    for (uint32_t i = 0; i < wm->windowCount; ++i)
    {
        window_index_entry &idx = wm->index[i];
        window_data_entry &data = *idx.dataEntry;

        for (uint64_t j = 0; j < data.pendingCookies.size; ++j)
        {
            xcb_atom_t property = data.pendingCookies[j].atom;
            xcb_get_property_cookie_t cook = data.pendingCookies[j].cookie;

            xcb_get_property_reply_t *reply = xcb_get_property_reply(X11GetConn(), cook, nullptr);

            switch (property)
            {
            case XCB_ATOM_WM_HINTS: {
                X11WmHints wmHints{};
                if (reply && xcb_get_property_value_length(reply) == (int)sizeof(X11WmHints))
                    wmHints = *static_cast<X11WmHints *>(xcb_get_property_value(reply));
                Initialize(wmHints);

                if (wmHints.input)
                    idx.flags |= window_index_entry::Flag_WM_Hints_Input;
                else
                    idx.flags &= ~window_index_entry::Flag_WM_Hints_Input;

                break;
            }

            case XCB_ATOM_WM_NORMAL_HINTS: {
                X11WmNormalHints nmHints{};
                if (reply && xcb_get_property_value_length(reply) == (int)sizeof(X11WmNormalHints))
                    nmHints = *static_cast<X11WmNormalHints *>(xcb_get_property_value(reply));
                Initialize(nmHints);
                data.maxWidth = (uint32_t)nmHints.maxWidth;
                data.maxHeight = (uint32_t)nmHints.maxHeight;
                break;
            }

            case XCB_ATOM_WM_NAME: {
                if (!reply)
                {
                    LOG("property fetch error");
                    break;
                }
                int len = xcb_get_property_value_length(reply);
                if (len < 0)
                    len = 0;
                uint64_t copyLen = Min((uint64_t)len, (uint64_t)63);
                InlineString::Assign(data.name,
                                     byteview{static_cast<uint8_t *>(xcb_get_property_value(reply)), copyLen});
                break;
            }

            case XCB_ATOM_WM_TRANSIENT_FOR: {
                if (!reply || !reply->length)
                    break;
                if (reply->type != XCB_ATOM_WINDOW)
                    break;
                if (idx.parent != 0)
                    break;
                idx.parent = *static_cast<xcb_window_t *>(xcb_get_property_value(reply));
                break;
            }

            default: {
                if (property != X11GetAtoms().wm_protocols)
                    break;
                if (!reply)
                    break;
                if (reply->type != XCB_ATOM_ATOM)
                    break;

                idx.flags &= ~window_index_entry::Flag_WM_TakeFocus;

                int numAtoms = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
                auto *atoms = static_cast<xcb_atom_t *>(xcb_get_property_value(reply));
                for (int k = 0; k < numAtoms; ++k)
                {
                    if (atoms[k] == X11GetAtoms().wm_take_focus)
                        idx.flags |= window_index_entry::Flag_WM_TakeFocus;
                }
                break;
            }
            }

            free(reply);
        }

        InlineVec::Clear(data.pendingCookies);
    }

    // Deferred restore: rearrange windows to their saved stacks/positions
    // and activate saved active windows. Uses XIDs directly — they survive
    // a WM restart since X11 keeps running.
    if (wm->restoreHasData && wm->pendingClients.size == 0)
    {
        {
            // Move windows to their saved stacks (append only — order is fixed below)
            for (uint32_t i = 0; i < wm->windowCount; ++i)
            {
                window_data_entry &data = wm->data[i];
                restore_entry *re = FindRestorePos(data.window);
                if (!re)
                    continue;

                // Restore saved tier
                data.tierIndex = re->tierIndex;

                // Remove from current stack (if any)
                window_stack &curStack = wm->stacks[wm->activeStackIndex];
                xcb_window_t *pos = InlineVec::Find(curStack.windows, data.window);
                if (pos)
                    InlineVec::Erase(curStack.windows, pos);

                // Add to saved stack
                window_stack &savedStack = wm->stacks[re->stackIndex];
                if (!InlineVec::Find(savedStack.windows, data.window))
                    InlineVec::Append(savedStack.windows, data.window);
            }

            // Reorder each stack's windows to match saved positions.
            // The append loop above places windows in data-iteration order,
            // which does not match the saved layout order. We rebuild each
            // stack by position so the left-to-right visual order is preserved.
            for (int s = 0; s < 9; ++s)
            {
                window_stack &stk = wm->stacks[s];
                uint64_t n = stk.windows.size;
                if (n <= 1)
                    continue;

                // Collect current windows for this stack
                inline_vec<xcb_window_t, 64> unsorted{};
                for (uint64_t j = 0; j < n; ++j)
                    InlineVec::Append(unsorted, stk.windows[j]);

                InlineVec::Clear(stk.windows);
                // For each position 0..n-1, find the window that belongs there
                for (uint64_t pos = 0; pos < n; ++pos)
                {
                    for (uint64_t j = 0; j < unsorted.size; ++j)
                    {
                        xcb_window_t w = unsorted[j];
                        restore_entry *re = FindRestorePos(w);
                        if (re && re->position == pos)
                        {
                            InlineVec::Append(stk.windows, w);
                            break;
                        }
                    }
                }
            }

            // Apply saved active stack index — must happen before activation
            // so the correct active window gets X11 focus.
            wm->activeStackIndex = wm->restoreActiveStackIndex;

            // Activate saved active windows on each stack
            for (int s = 0; s < 9; ++s)
            {
                xcb_window_t savedWin = wm->savedActiveXids[s];
                if (!savedWin || !FindIndex(savedWin))
                    continue;
                wm->stacks[s].activeWindow = savedWin;
                if (s == wm->activeStackIndex)
                    Activate(wm->stacks[s], savedWin, XCB_CURRENT_TIME);
            }

            wm->layoutDirty = true;
            wm->follow = false;

            // Clear restore data so it doesn't affect windows spawned later.
            wm->restoreHasData = false;
            InlineVec::Clear(wm->restoreMap);
        }
    }

    window_stack &stack = GetActiveStack();

    // Process newly managed clients
    if (wm->pendingClients.size > 0)
    {
        // First pass: resolve transient-for chains
        for (uint64_t i = 0; i < wm->pendingClients.size; ++i)
        {
            xcb_window_t clientWindow = wm->pendingClients[i];
            window_index_entry *idx = FindIndex(clientWindow);
            if (!idx)
                continue;

            if (idx->parent)
            {
                bool found = false;
                for (int j = 0; j < 10; ++j)
                {
                    window_index_entry *parentIdx = FindIndex(idx->parent);
                    if (!parentIdx)
                        break;
                    xcb_window_t nextParent = parentIdx->parent;
                    if (!nextParent)
                    {
                        found = true;
                        break;
                    }
                    idx->parent = nextParent;
                }
                if (!found)
                    idx->flags |= window_index_entry::Flag_PendingParent;
            }

            bool knownParent = idx->parent && !(idx->flags & window_index_entry::Flag_PendingParent);
            if (!knownParent || idx->parent != stack.activeWindow)
                ClearZoom(stack);
        }

        // Second pass: assign to stacks and activate
        bool activated = false;
        for (uint64_t i = 0; i < wm->pendingClients.size; ++i)
        {
            xcb_window_t clientWindow = wm->pendingClients[i];
            window_index_entry *idx = FindIndex(clientWindow);
            if (!idx)
                continue;

            // Spawn in parent's stack if transient-for is known; otherwise active stack.
            bool knownTransient = idx->parent && !(idx->flags & window_index_entry::Flag_PendingParent);
            window_stack *targetStack = &stack;
            if (knownTransient)
            {
                for (uint32_t s = 0; s < 9; ++s)
                {
                    if (InlineVec::Find(wm->stacks[s].windows, idx->parent))
                    {
                        targetStack = &wm->stacks[s];
                        break;
                    }
                }
            }

            {
                // Insert new windows adjacent to the active window (after it)
                // so they appear local to the viewport instead of at the end.
                xcb_window_t *insertPos = nullptr;
                if (targetStack == &stack && stack.activeWindow)
                {
                    xcb_window_t *activePos = InlineVec::Find(targetStack->windows, stack.activeWindow);
                    if (activePos)
                        insertPos = activePos + 1;
                }
                if (insertPos)
                    InlineVec::Insert(targetStack->windows, insertPos, clientWindow);
                else
                    InlineVec::Append(targetStack->windows, clientWindow);
                if (!activated && targetStack == &stack)
                {
                    Activate(stack, clientWindow, XCB_CURRENT_TIME);
                    activated = true;
                }
            }
        }

        InlineVec::Clear(wm->pendingClients);
        if (!wm->restoreHasData)
            wm->follow = false;
        wm->layoutDirty = true;
    }

    // Layout pass
    if (wm->layoutDirty)
    {
        auto hide = [](xcb_window_t clientWindow) {
            window_index_entry *idx = FindIndex(clientWindow);
            if (!idx)
                return;
            window_data_entry &data = *idx->dataEntry;
            Rect hideRect = {static_cast<int32_t>(X11GetScreen()->width_in_pixels),
                             static_cast<int32_t>(X11GetScreen()->height_in_pixels), data.rect.width, data.rect.height};
            ConfigureClientIfNeeded(clientWindow, *idx, data, hideRect, data.borderWidth);
        };

        // Hide inactive stacks' windows
        for (uint32_t istack = 0; istack < 9; ++istack)
        {
            if (istack != wm->activeStackIndex)
            {
                window_stack &s = wm->stacks[istack];
                for (uint64_t i = 0; i < s.windows.size; ++i)
                    hide(s.windows[i]);
            }
        }

        bool zoomed = !!(stack.flags & window_stack::FlagZoom);
        Rect screenRect = {X11GetMonitorX(), X11GetMonitorY(), X11GetMonitorWidth(), X11GetMonitorHeight()};
        if (!zoomed && screenRect.height > wm->barHeight)
            screenRect = {screenRect.x, static_cast<int32_t>(screenRect.y + wm->barHeight), screenRect.width,
                          screenRect.height - wm->barHeight};

        int32_t viewW = (int32_t)screenRect.width;
        int32_t viewH = (int32_t)screenRect.height;

        if (zoomed)
        {
            for (uint64_t i = 0; i < stack.windows.size; ++i)
            {
                xcb_window_t clientWindow = stack.windows[i];
                if (clientWindow != stack.activeWindow)
                {
                    hide(clientWindow);
                }
                else
                {
                    window_index_entry *idx = FindIndex(clientWindow);
                    if (idx)
                        ConfigureClientIfNeeded(clientWindow, *idx, *idx->dataEntry, screenRect, 2);
                }
            }
        }
        else if (stack.windows.size > 0)
        {
            uint32_t n = (uint32_t)stack.windows.size;
            uint32_t activeIndex = 0;
            xcb_window_t *activePos = InlineVec::Find(stack.windows, stack.activeWindow);
            if (activePos)
                activeIndex = (uint32_t)(activePos - InlineVec::DataPtr(stack.windows));

            inline_vec<int32_t, 64> winX{};
            inline_vec<uint32_t, 64> winW{};
            {
                uint32_t nw = (uint32_t)stack.windows.size;
                int32_t xc = 0;
                for (uint32_t j = 0; j < nw; ++j)
                {
                    xcb_window_t cw = stack.windows[j];
                    window_index_entry *cix = FindIndex(cw);
                    uint32_t w = cix ? TierWidth(cix->dataEntry->tierIndex) : kDefaultWindowWidth;
                    uint32_t capW = Min(w, (uint32_t)viewW);
                    if (cix && cix->dataEntry->maxWidth)
                        capW = Min(capW, cix->dataEntry->maxWidth);
                    capW = Max(capW, 100u);
                    InlineVec::Append(winX, xc);
                    InlineVec::Append(winW, capW);
                    xc += (int32_t)capW;
                }
            }

            int32_t contentEnd = InlineVec::Back(winX) + (int32_t)InlineVec::Back(winW);
            int32_t contentWidth = contentEnd;

            // When the strip fits within the viewport, center it by shifting
            // all window positions. This avoids negative scroll values that
            // X11 ConfigureWindow can't represent.
            if (contentWidth <= viewW)
            {
                int32_t centerOffset = (viewW - contentWidth) / 2;
                for (uint32_t i = 0; i < n; ++i)
                    winX[i] += centerOffset;
                contentEnd += centerOffset;
            }
            else
            {
                // Offset entire strip by 40px so the leftmost window has a gap.
                for (uint32_t i = 0; i < n; ++i)
                    winX[i] += 40;
                contentEnd += 40;
            }

            int32_t winLeft = winX[activeIndex];
            int32_t winRight = winLeft + (int32_t)winW[activeIndex];

            int32_t gap = 40;
            int32_t maxScroll = Max(0, contentEnd + gap - viewW);
            int32_t scroll = stack.scrollOffset;

            if (stack.flags & window_stack::FlagCenterScroll)
            {
                scroll = winLeft + (int32_t)winW[activeIndex] / 2 - viewW / 2;
            }
            else if (contentWidth > viewW)
            {
                int32_t lo = winRight - (viewW - gap); // min scroll (right edge in bounds)
                int32_t hi = winLeft - gap;            // max scroll (left edge in bounds)
                if (lo <= hi)
                    scroll = Clamp(scroll, lo, hi);
                else
                    scroll = winLeft + (int32_t)winW[activeIndex] / 2 - viewW / 2;
                scroll = Clamp(scroll, 0, maxScroll);
            }

            stack.scrollOffset = scroll;

            for (uint32_t i = 0; i < n; ++i)
            {
                xcb_window_t clientWindow = stack.windows[i];
                window_index_entry *idx = FindIndex(clientWindow);
                if (!idx)
                    continue;
                window_data_entry &data = *idx->dataEntry;

                Rect rect = {winX[i] - scroll, screenRect.y, winW[i], (uint32_t)viewH};
                if (rect.width > 4 && rect.height > 4)
                    rect = {rect.x, rect.y, rect.width - 4, rect.height - 4};

                if (data.maxHeight && rect.height > data.maxHeight)
                {
                    rect.y += (int32_t)((rect.height - data.maxHeight) / 2);
                    rect.height = data.maxHeight;
                }

                // Off-screen windows: move to constant off-screen position,
                // keeping their size. Avoids sending large coordinates to X11
                // (int16 limit) and reduces configure traffic.
                int32_t visRight = rect.x + (int32_t)rect.width;
                if (visRight <= 0 || rect.x >= viewW)
                {
                    rect.x = static_cast<int32_t>(X11GetScreen()->width_in_pixels);
                    rect.y = static_cast<int32_t>(X11GetScreen()->height_in_pixels);
                }

                ConfigureClientIfNeeded(clientWindow, *idx, data, rect, 2);
            }
        }

        wm->layoutDirty = false;
    }

    // Border update
    if (wm->borderDirty)
    {
        bool zoomed = !!(stack.flags & window_stack::FlagZoom);
        Color color = [&] -> Color {
            if (wm->follow)
                return Color::KActiveFollow;
            if (zoomed || stack.windows.size < 2)
                return Color::KNone;
            return Color::KActive;
        }();
        ApplyBorder(stack.activeWindow, color);

        // Clear inactive borders
        for (uint64_t i = 0; i < stack.windows.size; ++i)
        {
            if (stack.windows[i] != stack.activeWindow)
                ApplyBorder(stack.windows[i], Color::KNone);
        }

        wm->borderDirty = false;
    }

    // Send synthetic ConfigureNotify events
    for (uint32_t i = 0; i < wm->windowCount; ++i)
    {
        window_index_entry &idx = wm->index[i];
        if (!(idx.flags & window_index_entry::Flag_WantsConfigureNotify))
            continue;
        window_data_entry &data = *idx.dataEntry;
        X11SendConfigureNotify(idx.window, X11GetRoot(), (int16_t)(uint16_t)data.rect.x, (int16_t)(uint16_t)data.rect.y,
                               (uint16_t)data.rect.width, (uint16_t)data.rect.height, (uint16_t)data.borderWidth);
        idx.flags &= ~window_index_entry::Flag_WantsConfigureNotify;
    }

    // GDK may replace our event mask via XSelectInput (clipboard, DnD, etc.)
    {
        for (uint32_t i = 0; i < wm->windowCount; ++i)
        {
            xcb_get_window_attributes_reply_t *ar = xcb_get_window_attributes_reply(
                X11GetConn(), xcb_get_window_attributes(X11GetConn(), wm->index[i].window), nullptr);
            if (ar)
            {
                if ((ar->your_event_mask & kWmEventMask) != kWmEventMask)
                    xcb_change_window_attributes(X11GetConn(), wm->index[i].window, XCB_CW_EVENT_MASK, &kWmEventMask);
                free(ar);
            }
        }
    }

    // Publish state to overlay via shared memory
    if (wm->ipcChannel)
    {
        wm_ipc_state *ipc = static_cast<wm_ipc_state *>(ShmChannel::BeginWrite(*wm->ipcChannel));
        MemZero(ipc, sizeof(wm_ipc_state));
        ipc->activeStackIndex = wm->activeStackIndex;
        ipc->updateGeneration = ++sIpcGeneration;

        const window_stack &activeStack = GetActiveStack();
        if (activeStack.activeWindow)
        {
            window_index_entry *activeIdx = FindIndex(activeStack.activeWindow);
            if (activeIdx)
            {
                inline_string<64> &name = activeIdx->dataEntry->name;
                uint64_t n = Min<uint64_t>(name.size, sizeof(ipc->activeWindowTitle) - 1);
                if (n)
                    MemCpy(ipc->activeWindowTitle, name.data.data, n);
            }
        }

        ShmChannel::EndWrite(*wm->ipcChannel);
    }

    // Check pending window-attributes (INPUT_ONLY) cookies.
    // ManageClient flushes the request immediately, so the reply is
    // already waiting — _reply returns without blocking.
    {
        inline_vec<xcb_window_t, 16> inputOnlyWindows{};
        for (uint32_t i = 0; i < wm->windowCount; ++i)
        {
            window_data_entry &d = wm->data[i];
            if (!d.pendingAttrCookie.sequence)
                continue;
            xcb_get_window_attributes_reply_t *ar =
                xcb_get_window_attributes_reply(X11GetConn(), d.pendingAttrCookie, nullptr);
            d.pendingAttrCookie.sequence = 0;
            if (ar)
            {
                if (ar->_class == XCB_WINDOW_CLASS_INPUT_ONLY)
                    InlineVec::Append(inputOnlyWindows, d.window);
                free(ar);
            }
        }
        for (uint64_t i = 0; i < inputOnlyWindows.size; ++i)
            UnmanageClient(inputOnlyWindows[i]);
    }
}

// -- init --

void WmDeserialize()
{
    file_handle f = FileOpen({(uint8_t *)kStateFilePath, CStrLen(kStateFilePath, 32)}, FileOpenMode::Read);
    if (!FileValid(f))
        return;

    uint8_t buf[4096];
    uint32_t len = FileRead(f, 4096, buf);
    FileClose(f);

    // State is now in memory — delete the file so it doesn't leak
    // across boots or fresh sessions.
    unlink(kStateFilePath);

    if (len < 6) // magic + activeStackIndex + follow
        return;

    uint32_t pos = 0;
    auto readU32 = [&]() -> uint32_t {
        if (pos + 4 > len)
            return 0;
        uint32_t v;
        MemCpy(&v, buf + pos, 4);
        pos += 4;
        return v;
    };
    auto readU8 = [&]() -> uint8_t {
        if (pos >= len)
            return 0;
        return buf[pos++];
    };

    if (readU32() != kStateMagic)
        return;

    uint8_t savedActiveStackIndex = readU8();
    uint8_t savedFollow = readU8();

    // Bounds-check the active stack index
    if (savedActiveStackIndex >= 9)
        return;

    // Defer activeStackIndex — only applied in the restore block if windows
    // actually matched (genuine restart). A stale state file from a previous
    // boot has no matching windows and must not shift the active stack.
    wm->restoreActiveStackIndex = savedActiveStackIndex;
    wm->follow = (savedFollow != 0);

    for (int s = 0; s < 9; ++s)
    {
        // Per-stack header: layout (v1 only, now ignored), flags_lo, flags_hi, win_count
        if (pos + 4 > len)
            return;
        readU8(); // layout byte — ignored, always infinite strip now
        uint8_t flagsLo = readU8();
        uint8_t flagsHi = readU8();
        uint8_t winCount = readU8();

        // Bounds-check window count
        if (winCount > 64)
            return;

        // Apply saved flags (overriding WmInit defaults)
        wm->stacks[s].flags = (uint16_t)flagsLo | ((uint16_t)flagsHi << 8);

        for (uint8_t j = 0; j < winCount; ++j)
        {
            if (pos + 4 > len)
                return;
            xcb_window_t xid = (xcb_window_t)readU32();
            if (xid != 0 && InlineVec::Size(wm->restoreMap) < InlineVec::Capacity(wm->restoreMap))
            {
                restore_entry e = {xid, (uint8_t)s, j, 1};
                InlineVec::Append(wm->restoreMap, e);
            }
        }
        // per-window tier indices
        for (uint8_t j = 0; j < winCount; ++j)
        {
            if (pos + 4 > len)
                return;
            uint8_t ti = (uint8_t)readU32();
            for (uint64_t k = 0; k < wm->restoreMap.size; ++k)
            {
                if (wm->restoreMap[k].stackIndex == (uint8_t)s && wm->restoreMap[k].position == j)
                {
                    wm->restoreMap[k].tierIndex = ti;
                    break;
                }
            }
        }

        // activeWindow XID
        if (pos + 4 > len)
            return;
        wm->savedActiveXids[s] = (xcb_window_t)readU32();
    }

    wm->restoreHasData = true;
}

void WmInit()
{
    wm->barHeight = 20;
    RefreshTiers();

    // Only apply defaults to stacks that weren't populated by WmDeserialize
    if (!wm->restoreHasData)
    {
        for (auto &s : wm->stacks)
        {
            s.flags |= window_stack::FlagCenterScroll;
        }
    }

    if (xcb_request_check(X11GetConn(),
                          xcb_change_window_attributes_checked(
                              X11GetConn(), X11GetRoot(), XCB_CW_EVENT_MASK,
                              (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                           XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE})))
    {
        ASSERT(false && "another wm is already running");
    }

    wm->moveresizeActive = false;

    // EWMH _NET_SUPPORTING_WM_CHECK detection protocol
    {
        xcb_window_t checkWin = xcb_generate_id(X11GetConn());
        xcb_create_window(X11GetConn(), XCB_COPY_FROM_PARENT, checkWin, X11GetRoot(), 0, 0, 1, 1, 0,
                          XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0, nullptr);
        xcb_map_window(X11GetConn(), checkWin);

        // Set _NET_WM_NAME so tools can identify this WM
        static const char k_WMName[] = "nyla-wm";
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, checkWin, X11GetAtoms().net_wm_name,
                            X11GetAtoms().utf8_string, 8, sizeof(k_WMName) - 1, k_WMName);

        // Self-referencing: checkWin._NET_SUPPORTING_WM_CHECK = checkWin
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, checkWin, X11GetAtoms().net_supporting_wm_check,
                            XCB_ATOM_WINDOW, 32, 1, &checkWin);
        // Root._NET_SUPPORTING_WM_CHECK = checkWin
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, X11GetRoot(), X11GetAtoms().net_supporting_wm_check,
                            XCB_ATOM_WINDOW, 32, 1, &checkWin);
    }

    // Advertise EWMH features
    {
        const xcb_atom_t supportedAtoms[] = {X11GetAtoms().net_wm_moveresize};
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, X11GetRoot(), X11GetAtoms().net_supported,
                            XCB_ATOM_ATOM, 32, sizeof(supportedAtoms) / sizeof(xcb_atom_t), supportedAtoms);
    }

    X11Grab();

    xcb_query_tree_reply_t *treeReply =
        xcb_query_tree_reply(X11GetConn(), xcb_query_tree(X11GetConn(), X11GetRoot()), nullptr);
    if (treeReply)
    {
        xcb_window_t *children = xcb_query_tree_children(treeReply);
        int numChildren = xcb_query_tree_children_length(treeReply);

        for (int i = 0; i < numChildren; ++i)
        {
            xcb_window_t w = children[i];
            xcb_get_window_attributes_reply_t *attrReply =
                xcb_get_window_attributes_reply(X11GetConn(), xcb_get_window_attributes(X11GetConn(), w), nullptr);
            if (!attrReply)
                continue;
            bool skip = attrReply->override_redirect || attrReply->map_state == XCB_MAP_STATE_UNMAPPED ||
                        attrReply->_class == XCB_WINDOW_CLASS_INPUT_ONLY;
            free(attrReply);
            if (skip)
                continue;
            ManageClient(w);
        }

        free(treeReply);
    }

    auto grabKey = [](int meta, int alt, int ctrl, int shift, KeyPhysical key) {
        uint32_t mod = 0;
        if (meta)
            mod |= XCB_MOD_MASK_4;
        if (alt)
            mod |= XCB_MOD_MASK_1;
        if (ctrl)
            mod |= XCB_MOD_MASK_CONTROL;
        if (shift)
            mod |= XCB_MOD_MASK_SHIFT;
        uint32_t keycode = X11KeyPhysicalToKeyCode(key);
        const xcb_generic_error_t *err =
            xcb_request_check(X11GetConn(), xcb_grab_key_checked(X11GetConn(), 1, X11GetRoot(), mod, keycode,
                                                                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC));
        ASSERT(!err);
    };

    grabKey(1, 0, 0, 0, KeyPhysical::Backspace);
    grabKey(0, 1, 0, 0, KeyPhysical::F4);
    grabKey(1, 0, 0, 0, KeyPhysical::S);
    grabKey(1, 0, 0, 0, KeyPhysical::D);
    grabKey(1, 0, 0, 0, KeyPhysical::E);
    grabKey(1, 0, 0, 0, KeyPhysical::R);
    grabKey(1, 0, 0, 0, KeyPhysical::F);
    grabKey(0, 1, 0, 1, KeyPhysical::Tab);
    grabKey(0, 1, 0, 0, KeyPhysical::Tab);
    grabKey(1, 0, 0, 0, KeyPhysical::G);
    grabKey(1, 0, 0, 0, KeyPhysical::V);
    grabKey(1, 0, 0, 0, KeyPhysical::T);
    grabKey(1, 0, 1, 0, KeyPhysical::ArrowLeft);
    grabKey(1, 0, 1, 0, KeyPhysical::ArrowRight);
    grabKey(1, 0, 0, 0, KeyPhysical::ArrowLeft);
    grabKey(1, 0, 0, 0, KeyPhysical::ArrowRight);
    grabKey(1, 0, 0, 1, KeyPhysical::R);

    X11Flush();
    X11Ungrab();
    X11Flush();
}

} // namespace

// -- entry point --

void UserMain()
{
    struct sigaction sa = {};
    sa.sa_handler = [](int) { TRAP(); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ASSERT(sigaction(SIGINT, &sa, nullptr) != -1);

    wm = &RegionAlloc::Alloc<window_manager>(RegionAlloc::g_BootstrapAlloc);
    wm->memory = MemPagePool::AcquireChunk();
    wm->capacity = 16;
    wm->index = (window_index_entry *)wm->memory.data;
    wm->data = (window_data_entry *)(wm->index + wm->capacity);
    CommitMemPages(wm->memory.data, (uint8_t *)(wm->data + wm->capacity) - wm->memory.data);

    WmDeserialize();
    WmInit();
    LOG("[wm] init done");

    // IPC channel — suppressed when NYLA_WM_NO_DAEMONS=1 (test harness)
    // to avoid the test WM writing to the host's /dev/shm/nyla_wm and
    // corrupting the real wm_overlay's status bar.
    if (!getenv("NYLA_WM_NO_DAEMONS"))
        wm->ipcChannel = ShmChannel::CreateWriter("nyla_wm", sizeof(wm_ipc_state), RegionAlloc::g_BootstrapAlloc);

    // Launch overlay and daemons (best-effort, non-blocking).
    // Suppressed when NYLA_WM_NO_DAEMONS=1 (test harness).
    if (!getenv("NYLA_WM_NO_DAEMONS"))
    {
        // Launch overlay. Resolve wm_overlay path relative to wm binary.
        (void)!system("pkill wm_overlay 2>/dev/null");
        {
            char wmPath[256];
            ssize_t len = readlink("/proc/self/exe", wmPath, sizeof(wmPath) - 1);
            if (len > 0)
            {
                wmPath[len] = '\0';
                // Find last '/' to get the directory
                char *lastSlash = strrchr(wmPath, '/');
                if (lastSlash)
                {
                    *lastSlash = '\0';
                    byteview overlayPath = StringWriteFmt("%s/wm_overlay"_s, (uint8_t *)wmPath);
                    const char *const overlayCmd[] = {(const char *)overlayPath.data, nullptr};
                    Spawn({overlayCmd, 2});
                }
            }
        }

        // Launch startup daemons (only if not already running).
        if (system("pgrep -x dunst >/dev/null 2>&1") != 0)
        {
            const char *const dunstCmd[] = {"dunst", nullptr};
            Spawn({dunstCmd, 2});
        }
        if (system("pgrep -x redshift >/dev/null 2>&1") != 0)
        {
            const char *const redshiftCmd[] = {"redshift", "-l", "49:8", nullptr};
            Spawn({redshiftCmd, 4});
        }
    }

    bool isRunning = true;
    while (isRunning && !xcb_connection_has_error(X11GetConn()))
    {
        pollfd fd{
            .fd = xcb_get_file_descriptor(X11GetConn()),
            .events = POLLIN,
        };
        if (poll(&fd, 1, -1) == -1)
        {
            if (errno == EINTR)
                continue;
            LOG("poll(): %s", strerror(errno));
            continue;
        }
        if (fd.revents & POLLIN)
        {
            WmProcess(isRunning);
        }
        X11Flush();
    }

    LOG("exiting");
}

} // namespace nyla