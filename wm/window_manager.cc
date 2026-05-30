// WM — tiling window manager for X11.
//
// Infinite strip tiles windows horizontally at their native pixel widths. The
// strip can extend beyond the screen; the active window stays visible via
// auto-scroll. New windows never resize existing ones. Meta+←/→ adjusts the
// active window's width without affecting neighbors.
//
// The same mod-key grammar applies everywhere:
//   Meta+Ltr    — window ops (move, toggle zoom/follow)
//   Meta+←/→   — resize active window width
//   Meta+Ctrl+←/→ — switch stacks (workspaces)
//   Alt+Tab     — cycle windows
//   Alt+F4      — close window

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "nyla/commons/array.h"
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
#include "nyla/commons/span.h"
#include "nyla/commons/time.h"
#include "nyla/commons/wm_ipc.h"
#include "nyla/commons/x11_wm_hints_linux.h"

namespace nyla
{

namespace
{

// ─── Rect ────────────────────────────────────────────────────────────────────

struct Rect
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(Rect) == 16);

auto TryApplyPadding(const Rect &r, uint32_t padding) -> Rect
{
    if (r.width > 2 * padding && r.height > 2 * padding)
        return {r.x, r.y, r.width - 2 * padding, r.height - 2 * padding};
    return r;
}

auto TryApplyMarginTop(const Rect &r, uint32_t marginTop) -> Rect
{
    if (r.height > marginTop)
        return {r.x, static_cast<int32_t>(r.y + marginTop), r.width, r.height - marginTop};
    return r;
}

// ─── Data structures ─────────────────────────────────────────────────────────

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
    inline_vec<xcb_window_t, 16> focusHistory;

    static inline constexpr decltype(flags) FlagZoom = 1 << 0;
    static inline constexpr decltype(flags) FlagCenterScroll = 1 << 1;
};

// ─── NET WM constants ───────────────────────────────────────────────────────

enum NetWmMoveresizeDirection : uint32_t
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

struct pending_property
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
    uint32_t desiredWidth; // Current pixel width on screen
    uint32_t baseWidth;    // Original 1× width, stable reference for tier computation
    Rect rect;             // Current on-screen position
    inline_string<64> name;
    inline_vec<xcb_window_t, 8> subwindows;
    inline_vec<pending_property, 8> pendingCookies;
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

// Maps a window XID to its saved stack index and position for WM restart recovery.
struct restore_entry
{
    xcb_window_t window;
    uint8_t stackIndex;
    uint8_t position; // index within the stack's windows list
    uint32_t desiredWidth;
    uint32_t baseWidth;
};

struct window_manager
{
    uint8_t activeStackIndex;
    bool layoutDirty;
    bool borderDirty;
    bool follow;
    bool focusCheckPending;
    bool restoreHasData; // true until deferred restore completes

    uint32_t barHeight;
    uint64_t lastRandRRefreshMs;
    xcb_window_t lastEnteredWindow;
    xcb_get_input_focus_cookie_t focusCheckCookie;

    // _NET_WM_MOVERESIZE grab state
    bool moveresizeActive;
    xcb_window_t moveresizeWindow;
    int32_t moveresizeOrigPointerX;
    uint32_t moveresizeOrigWidth;
    uint32_t moveresizeSnappedWidth;
    NetWmMoveresizeDirection moveresizeDirection;

    array<window_stack, 9> stacks;
    inline_vec<xcb_window_t, 64> pendingClients;
    inline_vec<restore_entry, 576> restoreMap; // 9 stacks × 64 windows max
    xcb_window_t savedActiveXids[9];           // per-stack active XID from WmDeserialize
    uint8_t restoreActiveStackIndex;           // deferred; only applied if windows matched

    span<uint8_t> memory;
    uint32_t capacity;
    uint32_t windowCount;
    window_index_entry *index;
    window_data_entry *data;

    shm_channel *ipcChannel = nullptr;
};

window_manager *wm;
static uint64_t sIpcGeneration; // incremented on every shm write

// ─── Capacity ────────────────────────────────────────────────────────────────

void IncreaseCapacity()
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

// ─── Index helpers ───────────────────────────────────────────────────────────

auto FindIndex(xcb_window_t w) -> window_index_entry *
{
    return BinarySearch::Find(span<window_index_entry>{wm->index, wm->windowCount}, w,
                              [](const window_index_entry &e) { return e.window; });
}

void RemoveWindow(xcb_window_t clientWindow)
{
    span<window_index_entry> s{wm->index, wm->windowCount};
    window_index_entry *idx = BinarySearch::Find(s, clientWindow, [](const window_index_entry &e) { return e.window; });
    ASSERT(idx);
    uint32_t idxPos = (uint32_t)(idx - wm->index);
    uint32_t dataPos = (uint32_t)(idx->dataEntry - wm->data);
    uint32_t last = wm->windowCount - 1;
    if (dataPos != last)
    {
        wm->data[dataPos] = wm->data[last];
        window_index_entry *movedIdx =
            BinarySearch::Find(s, wm->data[dataPos].window, [](const window_index_entry &e) { return e.window; });
        ASSERT(movedIdx);
        movedIdx->dataEntry = &wm->data[dataPos];
    }
    MemMove(&wm->index[idxPos], &wm->index[idxPos + 1], (last - idxPos) * sizeof(window_index_entry));
    --wm->windowCount;
}

// ─── Stack helpers ────────────────────────────────────────────────────────────

auto GetActiveStack() -> window_stack &
{
    return wm->stacks[wm->activeStackIndex];
}

// Look up a window XID in the restore map and return its saved stack index and position.
// Returns nullptr if not found.
auto FindRestorePos(xcb_window_t w) -> restore_entry *
{
    for (uint64_t i = 0; i < wm->restoreMap.size; ++i)
    {
        if (wm->restoreMap[i].window == w)
            return &wm->restoreMap[i];
    }
    return nullptr;
}

// ─── Core operations ──────────────────────────────────────────────────────────

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
    pending_property &pp = InlineVec::Append(data.pendingCookies);
    pp = {property, cookie};
}

static constexpr uint32_t kDefaultWindowWidth = 1280;

void ManageClient(xcb_window_t clientWindow)
{
    if (FindIndex(clientWindow))
        return;

    // Read existing window attributes to both (a) skip INPUT_ONLY windows
    // that would appear as ghost slots (e.g. _NET_SUPPORTING_WM_CHECK) and
    // (b) preserve the client's own event selections when we set the WM mask.
    xcb_get_window_attributes_cookie_t attrCookie = xcb_get_window_attributes(X11GetConn(), clientWindow);
    xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(X11GetConn(), attrCookie, nullptr);
    if (!attr)
        return;
    if (attr->_class == XCB_WINDOW_CLASS_INPUT_ONLY)
    {
        free(attr);
        return;
    }

    if (wm->windowCount >= wm->capacity)
        IncreaseCapacity();

    uint32_t i = wm->windowCount++;
    MemZero(&wm->data[i]);
    wm->data[i].window = clientWindow;
    wm->data[i].desiredWidth = kDefaultWindowWidth;
    wm->data[i].baseWidth = kDefaultWindowWidth;
    uint64_t pos = BinarySearch::LowerBound(span<window_index_entry>{wm->index, (uint64_t)i}, clientWindow,
                                            [](const window_index_entry &e) { return e.window; });
    MemMove(&wm->index[pos + 1], &wm->index[pos], (i - pos) * sizeof(window_index_entry));
    MemZero(&wm->index[pos]);
    wm->index[pos].window = clientWindow;
    wm->index[pos].dataEntry = &wm->data[i];

    // Set the WM event mask, preserving the client's own selections.
    // Do NOT clobber the client's mask (e.g. Ghostty needs ButtonPress/PointerMotion).
    uint32_t eventMask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW |
                         XCB_EVENT_MASK_LEAVE_WINDOW | attr->your_event_mask;
    free(attr);
    xcb_change_window_attributes(X11GetConn(), clientWindow, XCB_CW_EVENT_MASK, &eventMask);

    // Hide off-screen immediately to prevent flash at wrong size
    xcb_configure_window(X11GetConn(), clientWindow,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                         (uint32_t[]){X11GetScreen()->width_in_pixels, X11GetScreen()->height_in_pixels, 1, 1});

    window_data_entry &data = wm->data[i];
    data.rect = {static_cast<int32_t>(X11GetScreen()->width_in_pixels),
                 static_cast<int32_t>(X11GetScreen()->height_in_pixels), 1, 1};
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_HINTS);
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_NORMAL_HINTS);
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_NAME);
    FetchClientProperty(clientWindow, data, XCB_ATOM_WM_TRANSIENT_FOR);
    FetchClientProperty(clientWindow, data, X11GetAtoms().wm_protocols);

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
        // Push the previous active window onto focus history, avoiding duplicates
        if (stack.activeWindow)
        {
            // Remove any existing entry to avoid duplicates
            xcb_window_t *existing = InlineVec::Find(stack.focusHistory, stack.activeWindow);
            if (existing)
                InlineVec::Erase(stack.focusHistory, existing);
            InlineVec::Append(stack.focusHistory, stack.activeWindow);
        }
        ApplyBorder(stack.activeWindow, Color::KNone);
        stack.activeWindow = clientWindow;
        wm->layoutDirty = true;
    }
    Activate(stack, time);
}

void AdoptPendingTransients(xcb_window_t parentWindow)
{
    window_index_entry *parentIdx = FindIndex(parentWindow);
    if (!parentIdx)
        return;

    for (uint32_t i = 0; i < wm->windowCount; ++i)
    {
        window_index_entry &childIdx = wm->index[i];
        if (!(childIdx.flags & window_index_entry::Flag_PendingParent))
            continue;
        if (childIdx.parent != parentWindow)
            continue;

        xcb_window_t childWindow = childIdx.window;

        for (uint32_t istack = 0; istack < 9; ++istack)
        {
            window_stack &s = wm->stacks[istack];
            xcb_window_t *pos = InlineVec::Find(s.windows, childWindow);
            if (!pos)
                continue;

            InlineVec::Erase(s.windows, pos);
            if (s.activeWindow == childWindow)
            {
                if (s.windows.size > 0)
                    s.activeWindow = s.windows[0];
                else
                    s.activeWindow = 0;
                if (istack == wm->activeStackIndex)
                    Activate(s, s.activeWindow, XCB_CURRENT_TIME);
            }
            break;
        }

        InlineVec::Append(parentIdx->dataEntry->subwindows, childWindow);
        childIdx.flags &= ~window_index_entry::Flag_PendingParent;
        wm->layoutDirty = true;
    }
}

void ClearZoom(window_stack &stack)
{
    if (!(stack.flags & window_stack::FlagZoom))
        return;
    stack.flags &= ~window_stack::FlagZoom;
    wm->layoutDirty = true;
}

void DoFocusTheftCheck(xcb_window_t focusedWindow)
{
    const window_stack &stack = GetActiveStack();
    if (stack.activeWindow == focusedWindow)
        return;
    if (focusedWindow == X11GetRoot())
        return;
    if (!focusedWindow)
        return;

    if (!FindIndex(focusedWindow))
    {
        for (;;)
        {
            xcb_query_tree_reply_t *qr =
                xcb_query_tree_reply(X11GetConn(), xcb_query_tree(X11GetConn(), focusedWindow), nullptr);
            if (!qr)
            {
                Activate(stack, XCB_CURRENT_TIME);
                return;
            }
            xcb_window_t par = qr->parent;
            free(qr);
            if (!par || par == X11GetRoot())
                break;
            focusedWindow = par;
        }
    }

    if (stack.activeWindow == focusedWindow)
        return;

    window_index_entry *idx = FindIndex(focusedWindow);
    if (!idx)
    {
        Activate(stack, XCB_CURRENT_TIME);
        return;
    }

    if (idx->parent)
    {
        if (idx->parent == stack.activeWindow)
            return;
        window_index_entry *activeIdx = FindIndex(stack.activeWindow);
        if (activeIdx && idx->parent == activeIdx->parent)
            return;
    }

    Activate(stack, XCB_CURRENT_TIME);
}

void MaybeActivateLastEntered(window_stack &stack, xcb_timestamp_t ts)
{
    if (stack.flags & window_stack::FlagZoom)
        return;
    if (wm->follow)
        return;
    if (!wm->lastEnteredWindow)
        return;
    if (wm->lastEnteredWindow == X11GetRoot())
        return;
    if (wm->lastEnteredWindow == stack.activeWindow)
        return;
    if (FindIndex(wm->lastEnteredWindow))
        Activate(stack, wm->lastEnteredWindow, ts);
}

void UnmanageClient(xcb_window_t clientWindow)
{
    window_index_entry *idx = FindIndex(clientWindow);
    if (!idx)
        return;

    window_data_entry &data = *idx->dataEntry;

    for (uint64_t j = 0; j < data.pendingCookies.size; ++j)
        xcb_discard_reply(X11GetConn(), data.pendingCookies[j].cookie.sequence);

    bool realTransient = idx->parent && !(idx->flags & window_index_entry::Flag_PendingParent);
    if (realTransient)
    {
        ASSERT(data.subwindows.size == 0);
        window_index_entry *parentIdx = FindIndex(idx->parent);
        if (parentIdx)
        {
            xcb_window_t *pos = InlineVec::Find(parentIdx->dataEntry->subwindows, clientWindow);
            if (pos)
                InlineVec::Erase(parentIdx->dataEntry->subwindows, pos);
        }
    }
    else
    {
        for (uint64_t j = 0; j < data.subwindows.size; ++j)
        {
            window_index_entry *childIdx = FindIndex(data.subwindows[j]);
            if (childIdx)
                childIdx->parent = 0;
        }
        for (uint32_t i = 0; i < wm->windowCount; ++i)
        {
            window_index_entry &orphan = wm->index[i];
            if ((orphan.flags & window_index_entry::Flag_PendingParent) && orphan.parent == clientWindow)
            {
                orphan.flags &= ~window_index_entry::Flag_PendingParent;
                orphan.parent = 0;
            }
        }
    }

    xcb_window_t transientFor;
    if (realTransient)
        transientFor = idx->parent;
    else
        transientFor = 0;
    RemoveWindow(clientWindow);

    // Clean up any focus history entries pointing to the removed window
    for (uint32_t istack = 0; istack < 9; ++istack)
    {
        window_stack &s = wm->stacks[istack];
        for (uint64_t j = 0; j < s.focusHistory.size;)
        {
            if (s.focusHistory[j] == clientWindow)
                InlineVec::Erase(s.focusHistory, &s.focusHistory[j]);
            else
                ++j;
        }
    }

    for (uint32_t istack = 0; istack < 9; ++istack)
    {
        window_stack &stack = wm->stacks[istack];
        xcb_window_t lookFor;
        if (transientFor)
            lookFor = transientFor;
        else
            lookFor = clientWindow;
        xcb_window_t *winPos = InlineVec::Find(stack.windows, lookFor);
        if (!winPos)
            continue;

        wm->layoutDirty = true;
        if (!transientFor)
        {
            wm->follow = false;
            stack.flags &= ~window_stack::FlagZoom;
            InlineVec::Erase(stack.windows, winPos);
        }

        if (stack.activeWindow == clientWindow)
        {
            stack.activeWindow = 0;
            if (istack == wm->activeStackIndex)
            {
                xcb_window_t fallback = transientFor;
                if (!fallback)
                {
                    // Pop the most recent entry from focus history, skipping
                    // the destroyed window.
                    while (stack.focusHistory.size > 0)
                    {
                        fallback = InlineVec::PopBack(stack.focusHistory);
                        if (fallback != clientWindow && InlineVec::Find(stack.windows, fallback))
                            break;
                        fallback = 0;
                    }
                }
                if (!fallback && stack.windows.size > 0)
                    fallback = stack.windows[0];
                Activate(stack, fallback, XCB_CURRENT_TIME);
            }
        }
        return;
    }
}

// ─── Actions ─────────────────────────────────────────────────────────────────

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

void CloseActive()
{
    window_stack &stack = GetActiveStack();
    if (!stack.activeWindow)
        return;

    static uint64_t last = 0;
    uint64_t now = GetMonotonicTimeMillis();
    if (now - last >= 100)
        X11SendWmDeleteWindow(stack.activeWindow);
    last = now;
}

void ToggleZoom()
{
    window_stack &stack = GetActiveStack();
    stack.flags ^= window_stack::FlagZoom;
    wm->layoutDirty = true;
    wm->borderDirty = true;
}

void ToggleFollow()
{
    window_stack &stack = GetActiveStack();

    if (!stack.activeWindow)
    {
        wm->follow = false;
        return;
    }

    window_index_entry *idx = FindIndex(stack.activeWindow);
    if (!idx || idx->parent)
    {
        wm->follow = false;
        return;
    }

    wm->follow = !wm->follow;
    if (!wm->follow)
        ClearZoom(stack);
    wm->borderDirty = true;
}

// Discrete width tiers (no wrap-around, stops at edges):
//   0.75×  = baseWidth * 3/4
//   1×     = baseWidth (default)
//   1.5×   = baseWidth * 3/2  (skipped if >= 80% of viewport)
//   full   = viewport width
// Snaps to nearest tier first, then steps by one.
void ResizeActive(int32_t direction)
{
    window_stack &stack = GetActiveStack();
    if (!stack.activeWindow)
        return;
    window_index_entry *idx = FindIndex(stack.activeWindow);
    if (!idx)
        return;

    uint32_t viewW = (uint32_t)X11GetMonitorWidth();
    uint32_t base = idx->dataEntry->baseWidth;

    uint32_t tiers[4];
    uint32_t nTiers = 0;
    tiers[nTiers++] = Max(base - base / 4, 320u);
    tiers[nTiers++] = base;
    uint32_t mid = Min(base + base / 2, viewW);
    if (mid < viewW * 4 / 5)
        tiers[nTiers++] = mid;
    if (tiers[nTiers - 1] != viewW)
        tiers[nTiers++] = viewW;

    // Snap to nearest tier
    int32_t best = 0;
    {
        uint32_t cur = idx->dataEntry->desiredWidth;
        uint32_t bestD = cur < tiers[0] ? tiers[0] - cur : cur - tiers[0];
        for (uint32_t i = 1; i < nTiers; ++i)
        {
            uint32_t d = cur < tiers[i] ? tiers[i] - cur : cur - tiers[i];
            if (d < bestD)
            {
                bestD = d;
                best = (int32_t)i;
            }
        }
    }

    int32_t next = best + direction;
    if (next < 0 || next >= (int32_t)nTiers)
        return;

    idx->dataEntry->desiredWidth = tiers[(uint32_t)next];
    wm->layoutDirty = true;
}

void ToggleCenterScroll()
{
    window_stack &stack = GetActiveStack();
    if (!stack.activeWindow)
        return;

    stack.flags ^= window_stack::FlagCenterScroll;
    wm->layoutDirty = true;
}

// ─── Infinite strip layout ───────────────────────────────────────────────────

// Compute widths and natural X positions for each window in a strip,
// using pixel-based desiredWidth, capped to viewport width.
void ComputeInfiniteStripLayout(const window_stack &stack, int32_t viewW, inline_vec<int32_t, 64> &winX,
                                inline_vec<uint32_t, 64> &winW)
{
    uint32_t n = (uint32_t)stack.windows.size;
    int32_t xCursor = 0;

    for (uint32_t i = 0; i < n; ++i)
    {
        xcb_window_t clientWindow = stack.windows[i];
        window_index_entry *idx = FindIndex(clientWindow);
        uint32_t w = kDefaultWindowWidth;
        if (idx && idx->dataEntry->desiredWidth)
            w = idx->dataEntry->desiredWidth;
        // Cap to viewport width so no single window exceeds the screen.
        // Also respect the client's own maxWidth if smaller.
        uint32_t capW = Min(w, (uint32_t)viewW);
        if (idx && idx->dataEntry->maxWidth)
            capW = Min(capW, idx->dataEntry->maxWidth);
        capW = Max(capW, 100u);
        InlineVec::Append(winX, xCursor);
        InlineVec::Append(winW, capW);
        xCursor += (int32_t)capW;
    }
}

// ─── Process ─────────────────────────────────────────────────────────────────

void WmRestart();

void WmProcess(bool &isRunning)
{
    // Event loop
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
                WmRestart();
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
                ToggleZoom();
                break;
            }
            if (meta && key == KeyPhysical::V)
            {
                ToggleFollow();
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
                ToggleCenterScroll();
                break;
            }
            if (alt && key == KeyPhysical::F4)
            {
                CloseActive();
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
                            wm->moveresizeDirection = static_cast<NetWmMoveresizeDirection>(direction);
                            wm->moveresizeOrigWidth = idx->dataEntry->desiredWidth;
                            wm->moveresizeSnappedWidth = ((idx->dataEntry->desiredWidth + 80) / 160) * 160;
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
                        MaybeActivateLastEntered(stack, rb->time);
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
                uint32_t snapped = (uint32_t)Clamp((raw + 80) / 160 * 160, 640, 3840);
                if (snapped != wm->moveresizeSnappedWidth)
                {
                    window_index_entry *idx = FindIndex(wm->moveresizeWindow);
                    if (idx)
                    {
                        idx->dataEntry->desiredWidth = snapped;
                        wm->moveresizeSnappedWidth = snapped;
                        wm->layoutDirty = true;
                    }
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
            // Handle RandR screen change and notify events (hotplug).
            // Debounce: Xephyr fires a flood of RandR events when the WM
            // reconfigures CRTCs, creating a feedback loop. Only process
            // RandR refreshes at most once per second.
            uint32_t randrBase = X11RandRGetEventOffset();
            if (randrBase && (eventType == randrBase || eventType == randrBase + 1))
            {
                uint64_t now = GetMonotonicTimeMillis();
                if (now - wm->lastRandRRefreshMs >= 1000)
                {
                    wm->lastRandRRefreshMs = now;
                    if (X11RandRRefreshMonitors())
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

    // Deferred focus theft check (one round-trip per batch, not per event)
    if (wm->focusCheckPending)
    {
        auto *r = xcb_get_input_focus_reply(X11GetConn(), wm->focusCheckCookie, nullptr);
        if (r)
        {
            DoFocusTheftCheck(r->focus);
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

                // Restore saved widths
                data.desiredWidth = re->desiredWidth;
                data.baseWidth = re->baseWidth;

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

            if (idx->parent && !(idx->flags & window_index_entry::Flag_PendingParent))
            {
                window_index_entry *parentIdx = FindIndex(idx->parent);
                if (parentIdx)
                    InlineVec::Append(parentIdx->dataEntry->subwindows, clientWindow);
            }
            else
            {
                // Always spawn new windows on the active stack
                window_stack &targetStack = stack;

                // Insert new windows adjacent to the active window (after it)
                // so they appear local to the viewport instead of at the end.
                xcb_window_t *insertPos = nullptr;
                if (&targetStack == &stack && stack.activeWindow)
                {
                    xcb_window_t *activePos = InlineVec::Find(targetStack.windows, stack.activeWindow);
                    if (activePos)
                        insertPos = activePos + 1;
                }
                if (insertPos)
                    InlineVec::Insert(targetStack.windows, insertPos, clientWindow);
                else
                    InlineVec::Append(targetStack.windows, clientWindow);
                AdoptPendingTransients(clientWindow);
                if (!activated && &targetStack == &stack)
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

    // Layout pass — always runs before border update so positions are correct
    if (wm->layoutDirty)
    {
        auto hide = [](xcb_window_t clientWindow) -> window_index_entry * {
            window_index_entry *idx = FindIndex(clientWindow);
            if (!idx)
                return nullptr;
            window_data_entry &data = *idx->dataEntry;
            Rect hideRect = {static_cast<int32_t>(X11GetScreen()->width_in_pixels),
                             static_cast<int32_t>(X11GetScreen()->height_in_pixels), data.rect.width, data.rect.height};
            ConfigureClientIfNeeded(clientWindow, *idx, data, hideRect, data.borderWidth);
            return idx;
        };

        auto hideAll = [&hide](xcb_window_t clientWindow) {
            window_index_entry *idx = hide(clientWindow);
            if (!idx)
                return;
            for (uint64_t j = 0; j < idx->dataEntry->subwindows.size; ++j)
                hide(idx->dataEntry->subwindows[j]);
        };

        // Hide all windows from inactive stacks before configuring active stack
        for (uint32_t istack = 0; istack < 9; ++istack)
        {
            if (istack != wm->activeStackIndex)
            {
                window_stack &s = wm->stacks[istack];
                for (uint64_t i = 0; i < s.windows.size; ++i)
                    hideAll(s.windows[i]);
            }
        }

        bool zoomed = !!(stack.flags & window_stack::FlagZoom);
        Rect screenRect = {X11GetMonitorX(), X11GetMonitorY(), X11GetMonitorWidth(), X11GetMonitorHeight()};
        if (!zoomed)
            screenRect = TryApplyMarginTop(screenRect, wm->barHeight);

        int32_t viewW = (int32_t)screenRect.width;
        int32_t viewH = (int32_t)screenRect.height;

        if (zoomed)
        {
            for (uint64_t i = 0; i < stack.windows.size; ++i)
            {
                xcb_window_t clientWindow = stack.windows[i];
                if (clientWindow != stack.activeWindow)
                {
                    hideAll(clientWindow);
                }
                else
                {
                    window_index_entry *idx = FindIndex(clientWindow);
                    if (idx)
                    {
                        ConfigureClientIfNeeded(clientWindow, *idx, *idx->dataEntry, screenRect, 2);

                        // Position subwindows (transients/dialogs) centered
                        // within the viewport.
                        for (uint64_t j = 0; j < idx->dataEntry->subwindows.size; ++j)
                        {
                            xcb_window_t sub = idx->dataEntry->subwindows[j];
                            window_index_entry *subIdx = FindIndex(sub);
                            if (!subIdx)
                                continue;
                            window_data_entry &subData = *subIdx->dataEntry;
                            uint32_t subW = subData.desiredWidth ? subData.desiredWidth : kDefaultWindowWidth;
                            if (subData.maxWidth && subW > subData.maxWidth)
                                subW = subData.maxWidth;
                            uint32_t subH = subData.maxHeight ? subData.maxHeight : (uint32_t)viewH;
                            if (subW > (uint32_t)viewW)
                                subW = (uint32_t)viewW;
                            if (subH > (uint32_t)viewH)
                                subH = (uint32_t)viewH;
                            Rect subRect = {screenRect.x + ((int32_t)viewW - (int32_t)subW) / 2,
                                            screenRect.y + ((int32_t)viewH - (int32_t)subH) / 2, subW, subH};
                            ConfigureClientIfNeeded(sub, *subIdx, subData, subRect, 2);
                        }
                    }
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
            ComputeInfiniteStripLayout(stack, viewW, winX, winW);

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

            int32_t winLeft = winX[activeIndex];
            int32_t winRight = winLeft + (int32_t)winW[activeIndex];
            int32_t viewLeft = screenRect.x;
            int32_t viewRight = viewLeft + viewW;

            // Compute scroll to keep the active window visible.
            int32_t maxScroll = Max(0, contentEnd - viewW);
            int32_t scroll = stack.scrollOffset;

            if (stack.flags & window_stack::FlagCenterScroll)
            {
                // Center the active window in the viewport. No clamp on scroll;
                // first/last windows may need negative or beyond-maxScroll values
                // to properly center. The window positioning (winX[i] - scroll)
                // handles this correctly.
                scroll = winLeft + (int32_t)winW[activeIndex] / 2 - viewW / 2;
            }
            else
            {
                // Auto-scroll: ensure the active window is fully visible.
                if (winLeft - scroll < viewLeft)
                    scroll = winLeft - viewLeft;
                else if (winRight - scroll > viewRight)
                    scroll = winRight - viewRight;

                // Pin scroll to 0 when overflow is negligible.
                if (maxScroll < 16)
                    maxScroll = 0;

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

                Rect rect = TryApplyPadding({winX[i] - scroll, screenRect.y, winW[i], (uint32_t)viewH}, 2);

                if (data.maxHeight && rect.height > data.maxHeight)
                {
                    rect.y += (int32_t)((rect.height - data.maxHeight) / 2);
                    rect.height = data.maxHeight;
                }

                ConfigureClientIfNeeded(clientWindow, *idx, data, rect, 2);
            }
            // Position subwindows for the active window (non-zoomed).
            {
                window_index_entry *activeIdx = FindIndex(stack.activeWindow);
                if (activeIdx)
                {
                    for (uint64_t j = 0; j < activeIdx->dataEntry->subwindows.size; ++j)
                    {
                        xcb_window_t sub = activeIdx->dataEntry->subwindows[j];
                        window_index_entry *subIdx = FindIndex(sub);
                        if (!subIdx)
                            continue;
                        window_data_entry &subData = *subIdx->dataEntry;
                        uint32_t subW = subData.desiredWidth ? subData.desiredWidth : kDefaultWindowWidth;
                        if (subData.maxWidth && subW > subData.maxWidth)
                            subW = subData.maxWidth;
                        uint32_t subH = subData.maxHeight ? subData.maxHeight : (uint32_t)viewH;
                        if (subW > (uint32_t)viewW)
                            subW = (uint32_t)viewW;
                        if (subH > (uint32_t)viewH)
                            subH = (uint32_t)viewH;
                        Rect subRect = {screenRect.x + ((int32_t)viewW - (int32_t)subW) / 2,
                                        screenRect.y + ((int32_t)viewH - (int32_t)subH) / 2, subW, subH};
                        ConfigureClientIfNeeded(sub, *subIdx, subData, subRect, 2);
                    }
                }
            }
        }

        wm->layoutDirty = false;
    }

    // Border update — runs after layout so active window is at its final position
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

        // Clear borders from all other windows in the active stack
        for (uint64_t i = 0; i < stack.windows.size; ++i)
        {
            if (stack.windows[i] != stack.activeWindow)
                ApplyBorder(stack.windows[i], Color::KNone);
        }

        wm->borderDirty = false;
    }

    // Send synthetic configure notifies
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

    // Re-assert WM event mask on all managed windows.
    // GDK's gdk_x11_event_source_select_events calls XSelectInput which
    // REPLACES the event mask. Can happen at any time (clipboard, DnD,
    // subwindow setup), silently removing FOCUS_CHANGE etc. that the WM
    // needs for focus tracking.
    {
        constexpr uint32_t kWmEventMask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE |
                                          XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;
        for (uint32_t i = 0; i < wm->windowCount; ++i)
        {
            xcb_get_window_attributes_reply_t *ar = xcb_get_window_attributes_reply(
                X11GetConn(), xcb_get_window_attributes(X11GetConn(), wm->index[i].window), nullptr);
            if (ar)
            {
                if ((ar->your_event_mask & kWmEventMask) != kWmEventMask)
                {
                    uint32_t mask = kWmEventMask | ar->your_event_mask;
                    xcb_change_window_attributes(X11GetConn(), wm->index[i].window, XCB_CW_EVENT_MASK, &mask);
                }
                free(ar);
            }
        }
    }

    // Publish current state to the overlay via shared memory
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
}

// ─── Init ────────────────────────────────────────────────────────────────────

static constexpr const char *kStateFilePath = "/tmp/nyla_wm_state";
static constexpr uint32_t kStateMagic = 0x4E594C41; // "NYLA"
static constexpr uint32_t kStateVersion = 3;

// Write current WM state to disk so it can be restored on restart.
// On error, leaves a clean empty state file so the next startup doesn't crash.
void WmSerialize()
{
    uint8_t buf[4096];
    uint32_t pos = 0;

    auto writeU32 = [&](uint32_t v) {
        MemCpy(buf + pos, &v, 4);
        pos += 4;
    };
    auto writeU8 = [&](uint8_t v) { buf[pos++] = v; };

    writeU32(kStateMagic);
    writeU32(kStateVersion);
    writeU8(wm->activeStackIndex);
    writeU8(wm->follow ? 1 : 0);

    for (int s = 0; s < 9; ++s)
    {
        const window_stack &stack = wm->stacks[s];
        // Legacy layout byte (no longer used, always 0)
        writeU8(0);
        writeU8((uint8_t)(stack.flags & 0xFF));
        writeU8((uint8_t)(stack.flags >> 8));

        uint8_t winCount = (uint8_t)stack.windows.size;
        writeU8(winCount);
        for (uint64_t j = 0; j < stack.windows.size; ++j)
            writeU32((uint32_t)stack.windows[j]);
        for (uint64_t j = 0; j < stack.windows.size; ++j)
        {
            window_index_entry *idx = FindIndex(stack.windows[j]);
            if (idx)
            {
                writeU32(idx->dataEntry->desiredWidth);
                writeU32(idx->dataEntry->baseWidth);
            }
            else
            {
                writeU32(kDefaultWindowWidth);
                writeU32(kDefaultWindowWidth);
            }
        }

        uint8_t histCount = (uint8_t)stack.focusHistory.size;
        writeU8(histCount);
        for (uint64_t j = 0; j < stack.focusHistory.size; ++j)
            writeU32((uint32_t)stack.focusHistory[j]);

        uint32_t activeXid = (uint32_t)stack.activeWindow;
        writeU32(activeXid);
    }

    file_handle f = FileOpen({(uint8_t *)kStateFilePath, CStrLen(kStateFilePath, 32)}, FileOpenMode::Write);
    if (FileValid(f))
    {
        FileWrite(f, pos, buf);
        FileClose(f);
    }
}

// Read saved state from disk and populate wm->restoreMap + per-stack settings.
// Gracefully handles: missing file, zero-byte file, wrong magic, wrong version,
// truncated data. On any error the WM starts with default state.
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

    if (len < 12) // magic + version + activeStackIndex + follow = 10
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
    uint32_t savedVersion = readU32();
    // Accept both v1 (had layout byte) and v2+
    if (savedVersion < 1 || savedVersion > kStateVersion)
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
                restore_entry e = {xid, (uint8_t)s, j, kDefaultWindowWidth, kDefaultWindowWidth};
                InlineVec::Append(wm->restoreMap, e);
            }
        }
        // v3+: per-window desiredWidth and baseWidth
        if (savedVersion >= 3)
        {
            for (uint8_t j = 0; j < winCount; ++j)
            {
                if (pos + 8 > len)
                    return;
                uint32_t w = readU32();
                uint32_t b = readU32();
                for (uint64_t k = 0; k < wm->restoreMap.size; ++k)
                {
                    if (wm->restoreMap[k].stackIndex == (uint8_t)s && wm->restoreMap[k].position == j)
                    {
                        wm->restoreMap[k].desiredWidth = w;
                        wm->restoreMap[k].baseWidth = b;
                        break;
                    }
                }
            }
        }

        // focusHistory count
        if (pos >= len)
            return;
        uint8_t histCount = readU8();
        if (histCount > 16)
            return;
        for (uint8_t j = 0; j < histCount; ++j)
        {
            if (pos + 4 > len)
                return;
            readU32(); // skip — focus history XIDs; not used for restore
                       // (history is rebuilt naturally)
        }

        // activeWindow XID
        if (pos + 4 > len)
            return;
        wm->savedActiveXids[s] = (xcb_window_t)readU32();
    }

    wm->restoreHasData = true;
}

void WmRestart()
{
    if (wm->moveresizeActive)
    {
        xcb_ungrab_pointer(X11GetConn(), XCB_CURRENT_TIME);
        wm->moveresizeActive = false;
        wm->moveresizeWindow = XCB_NONE;
    }

    WmSerialize();

    // Release X11 grab and disconnect — the .xinitrc loop will restart us.
    X11Ungrab();
    xcb_disconnect(X11GetConn());

    // Close IPC channel so shm isn't left in a bad state
    if (wm->ipcChannel)
    {
        ShmChannel::Close(*wm->ipcChannel);
        wm->ipcChannel = nullptr;
    }
}

void WmInit()
{
    fprintf(stderr, "[wm] WmInit begin\n");
    fflush(stderr);
    wm->barHeight = 20;

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

    // ─── EWMH _NET_SUPPORTING_WM_CHECK ─────────────────────────────────────
    // Create an invisible check window. Set its own atom to point to itself,
    // and set the root atom to point to it. This is the standard EWMH
    // WM detection protocol. Without this, GDK's
    // gdk_x11_screen_supports_net_wm_hint() returns FALSE immediately.
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

    // ─── EWMH _NET_SUPPORTED ───────────────────────────────────────────────
    // Advertise which EWMH features we implement.
    {
        const xcb_atom_t supportedAtoms[] = {X11GetAtoms().net_wm_moveresize};
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, X11GetRoot(), X11GetAtoms().net_supported,
                            XCB_ATOM_ATOM, 32, sizeof(supportedAtoms) / sizeof(xcb_atom_t), supportedAtoms);
    }

    fprintf(stderr, "[wm] WmInit: X11Grab (skipped for test)\n");
    fflush(stderr);
    // X11Grab();
    fprintf(stderr, "[wm] WmInit: X11Grab done, querying tree\n");
    fflush(stderr);

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

    auto grabKey = [](bool meta, bool alt, bool ctrl, bool shift, KeyPhysical key) {
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

    fprintf(stderr, "[wm] WmInit: X11Flush\n");
    fflush(stderr);
    X11Flush();
    // X11Ungrab();
    fprintf(stderr, "[wm] WmInit: X11Ungrab done (skipped for test)\n");
    fflush(stderr);
}

} // namespace

// ─── Entry point ─────────────────────────────────────────────────────────────

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

    fprintf(stderr, "[wm] WmDeserialize start\n");
    fflush(stderr);
    WmDeserialize();
    fprintf(stderr, "[wm] WmDeserialize done\n");
    fflush(stderr);
    WmInit();
    fprintf(stderr, "[wm] WmInit returned, entering main loop\n");
    fflush(stderr);

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
                    char overlayPath[320];
                    snprintf(overlayPath, sizeof(overlayPath), "%s/wm_overlay", wmPath);
                    const char *const overlayCmd[] = {overlayPath, nullptr};
                    Spawn({overlayCmd, 2});
                }
            }
        }

        // Launch startup daemons.
        (void)!system("pkill dunst 2>/dev/null");
        {
            const char *const dunstCmd[] = {"dunst", nullptr};
            Spawn({dunstCmd, 2});
        }
        (void)!system("pkill redshift 2>/dev/null");
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
