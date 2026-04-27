#include "nyla/commons/array.h"
#include "nyla/commons/binary_search.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/linux/x11_wm_hints.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/platform_linux.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"
#include "nyla/commons/time.h"

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

    auto operator==(const Rect &) const -> bool = default;

    auto X() -> int32_t &
    {
        return x;
    }
    auto X() const -> const int32_t &
    {
        return x;
    }
    auto Y() -> int32_t &
    {
        return y;
    }
    auto Y() const -> const int32_t &
    {
        return y;
    }
    auto Width() -> uint32_t &
    {
        return width;
    }
    auto Width() const -> const uint32_t &
    {
        return width;
    }
    auto Height() -> uint32_t &
    {
        return height;
    }
    auto Height() const -> const uint32_t &
    {
        return height;
    }
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

auto TryApplyMargin(const Rect &r, uint32_t margin) -> Rect
{
    if (r.width > 2 * margin && r.height > 2 * margin)
        return {static_cast<int32_t>(r.x + margin), static_cast<int32_t>(r.y + margin), r.width - 2 * margin,
                r.height - 2 * margin};
    return r;
}

// ─── Layout ──────────────────────────────────────────────────────────────────

enum class LayoutType : uint8_t
{
    KColumns,
    KRows,
    KGrid,
};

void CycleLayoutType(LayoutType &layout)
{
    switch (layout)
    {
    case LayoutType::KColumns:
        layout = LayoutType::KRows;
        break;
    case LayoutType::KRows:
        layout = LayoutType::KGrid;
        break;
    case LayoutType::KGrid:
        layout = LayoutType::KColumns;
        break;
    default:
        UNREACHABLE();
    }
}

static void LayoutColumns(const Rect &r, uint32_t n, uint32_t padding, inline_vec<Rect, 64> &out)
{
    uint32_t w = r.width / n;
    for (uint32_t i = 0; i < n; ++i)
        InlineVec::Append(out, TryApplyPadding({static_cast<int32_t>(r.x + i * w), r.y, w, r.height}, padding));
}

static void LayoutRows(const Rect &r, uint32_t n, uint32_t padding, inline_vec<Rect, 64> &out)
{
    uint32_t h = r.height / n;
    for (uint32_t i = 0; i < n; ++i)
        InlineVec::Append(out, TryApplyPadding({r.x, static_cast<int32_t>(r.y + i * h), r.width, h}, padding));
}

static void LayoutGrid(const Rect &r, uint32_t n, uint32_t padding, inline_vec<Rect, 64> &out)
{
    if (n < 4)
    {
        LayoutColumns(r, n, padding, out);
        return;
    }
    uint32_t numCols = (uint32_t)std::floor(std::sqrt((double)n));
    uint32_t numRows = (uint32_t)std::ceil((double)n / numCols);
    uint32_t w = r.width / numCols;
    uint32_t h = r.height / numRows;
    for (uint32_t i = 0; i < n; ++i)
        InlineVec::Append(out, TryApplyPadding({static_cast<int32_t>(r.x + (i % numCols) * w),
                                                static_cast<int32_t>(r.y + (i / numCols) * h), w, h},
                                               padding));
}

void ComputeLayout(const Rect &bounds, uint32_t n, uint32_t padding, inline_vec<Rect, 64> &out,
                   LayoutType layoutType = LayoutType::KColumns)
{
    InlineVec::Clear(out);
    if (n == 0)
        return;
    if (n == 1)
    {
        InlineVec::Append(out, TryApplyPadding(bounds, padding));
        return;
    }
    switch (layoutType)
    {
    case LayoutType::KColumns:
        LayoutColumns(bounds, n, padding, out);
        break;
    case LayoutType::KRows:
        LayoutRows(bounds, n, padding, out);
        break;
    case LayoutType::KGrid:
        LayoutGrid(bounds, n, padding, out);
        break;
    }
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
    LayoutType layout;
    xcb_window_t activeWindow;
    inline_vec<xcb_window_t, 64> windows;

    static inline constexpr decltype(flags) FlagZoom = 1 << 0;
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
    Rect rect;
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
    static inline constexpr decltype(flags) Flag_WM_DeleteWindow = 1 << 2;
    static inline constexpr decltype(flags) Flag_Urgent = 1 << 3;
    static inline constexpr decltype(flags) Flag_WantsConfigureNotify = 1 << 4;
    static inline constexpr decltype(flags) Flag_PendingParent = 1 << 5;
};

struct window_manager
{
    uint8_t activeStackIndex;
    bool layoutDirty;
    bool borderDirty;
    bool follow;
    bool focusCheckPending;
    uint32_t barHeight;
    xcb_window_t lastEnteredWindow;
    xcb_get_input_focus_cookie_t focusCheckCookie;

    array<window_stack, 9> stacks;
    inline_vec<xcb_window_t, 64> pendingClients;

    span<uint8_t> memory;
    uint32_t capacity;
    uint32_t windowCount;
    window_index_entry *index;
    window_data_entry *data;
};

window_manager *wm;

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

void ManageClient(xcb_window_t clientWindow)
{
    if (FindIndex(clientWindow))
        return;

    if (wm->windowCount >= wm->capacity)
        IncreaseCapacity();

    uint32_t i = wm->windowCount++;
    MemZero(&wm->data[i]);
    wm->data[i].window = clientWindow;
    uint64_t pos = BinarySearch::LowerBound(span<window_index_entry>{wm->index, (uint64_t)i}, clientWindow,
                                            [](const window_index_entry &e) { return e.window; });
    MemMove(&wm->index[pos + 1], &wm->index[pos], (i - pos) * sizeof(window_index_entry));
    MemZero(&wm->index[pos]);
    wm->index[pos].window = clientWindow;
    wm->index[pos].dataEntry = &wm->data[i];

    const uint32_t eventMask =
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW;
    xcb_change_window_attributes(X11GetConn(), clientWindow, XCB_CW_EVENT_MASK, &eventMask);

    window_data_entry &data = wm->data[i];
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
        ApplyBorder(stack.activeWindow, Color::KNone);
        stack.activeWindow = clientWindow;
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
            if (!clearZoom)
                wm->layoutDirty = true;
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

void NextLayout()
{
    window_stack &stack = GetActiveStack();
    CycleLayoutType(stack.layout);
    ClearZoom(stack);
    wm->layoutDirty = true;
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

// ─── Process ─────────────────────────────────────────────────────────────────

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
            if (meta && key == KeyPhysical::W)
            {
                NextLayout();
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
            LOG("mapping notify");
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

        case XCB_ENTER_NOTIFY: {
            wm->lastEnteredWindow = reinterpret_cast<xcb_enter_notify_event_t *>(event)->event;
            break;
        }

        case 0: {
            auto *err = reinterpret_cast<xcb_generic_error_t *>(event);
            LOG("xcb error: %d, sequence: %d", err->error_code, err->sequence);
            break;
        }

        default:
            break;
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

                if (wmHints.Urgent())
                    idx.flags |= window_index_entry::Flag_Urgent;
                else
                    idx.flags &= ~window_index_entry::Flag_Urgent;

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
                InlineVec::Clear(data.name);
                MemCpy(InlineVec::DataPtr(data.name), xcb_get_property_value(reply), copyLen);
                data.name.size = copyLen;
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

                idx.flags &= ~window_index_entry::Flag_WM_DeleteWindow;
                idx.flags &= ~window_index_entry::Flag_WM_TakeFocus;

                int numAtoms = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
                auto *atoms = static_cast<xcb_atom_t *>(xcb_get_property_value(reply));
                for (int k = 0; k < numAtoms; ++k)
                {
                    if (atoms[k] == X11GetAtoms().wm_delete_window)
                        idx.flags |= window_index_entry::Flag_WM_DeleteWindow;
                    else if (atoms[k] == X11GetAtoms().wm_take_focus)
                        idx.flags |= window_index_entry::Flag_WM_TakeFocus;
                }
                break;
            }
            }

            free(reply);
        }

        InlineVec::Clear(data.pendingCookies);
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
                InlineVec::Append(stack.windows, clientWindow);
                AdoptPendingTransients(clientWindow);
                if (!activated)
                {
                    Activate(stack, clientWindow, XCB_CURRENT_TIME);
                    activated = true;
                }
            }
        }

        InlineVec::Clear(wm->pendingClients);
        wm->follow = false;
        wm->layoutDirty = true;
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
        wm->borderDirty = false;
    }

    // Layout pass
    if (wm->layoutDirty)
    {
        bool zoomed = !!(stack.flags & window_stack::FlagZoom);
        Rect screenRect = {0, 0, X11GetScreen()->width_in_pixels, X11GetScreen()->height_in_pixels};
        if (!zoomed)
            screenRect = TryApplyMarginTop(screenRect, wm->barHeight);

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

        auto configureWindows = [](Rect bounds, span<xcb_window_t> windows, LayoutType layoutType, auto visitor) {
            inline_vec<Rect, 64> layout;
            ComputeLayout(bounds, (uint32_t)windows.size, 2, layout, layoutType);
            ASSERT(layout.size == windows.size);

            for (uint64_t i = 0; i < layout.size; ++i)
            {
                xcb_window_t clientWindow = windows[i];
                window_index_entry *idx = FindIndex(clientWindow);
                if (!idx)
                    continue;
                window_data_entry &data = *idx->dataEntry;

                Rect rect = layout[i];
                if (data.maxWidth && rect.width > data.maxWidth)
                {
                    rect.x += (int32_t)((rect.width - data.maxWidth) / 2);
                    rect.width = data.maxWidth;
                }
                if (data.maxHeight && rect.height > data.maxHeight)
                {
                    rect.y += (int32_t)((rect.height - data.maxHeight) / 2);
                    rect.height = data.maxHeight;
                }

                ConfigureClientIfNeeded(clientWindow, *idx, data, rect, 2);
                visitor(data);
            }
        };

        auto configureSubwindows = [&configureWindows](window_data_entry &data) {
            if (data.subwindows.size > 0)
                configureWindows(TryApplyMargin(data.rect, 20), (span<xcb_window_t>)data.subwindows, LayoutType::KRows,
                                 [](window_data_entry &) {});
        };

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
                        uint32_t followFlag;
                        if (wm->follow)
                            followFlag = 2;
                        else
                            followFlag = 0;
                        ConfigureClientIfNeeded(clientWindow, *idx, *idx->dataEntry, screenRect, followFlag);
                        configureSubwindows(*idx->dataEntry);
                    }
                }
            }
        }
        else
        {
            configureWindows(screenRect, (span<xcb_window_t>)stack.windows, stack.layout, configureSubwindows);
        }

        for (uint32_t istack = 0; istack < 9; ++istack)
        {
            if (istack != wm->activeStackIndex)
            {
                window_stack &s = wm->stacks[istack];
                for (uint64_t i = 0; i < s.windows.size; ++i)
                    hideAll(s.windows[i]);
            }
        }

        wm->layoutDirty = false;
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
}

// ─── Init ────────────────────────────────────────────────────────────────────

void WmInit()
{
    wm->barHeight = 20;

    if (xcb_request_check(X11GetConn(),
                          xcb_change_window_attributes_checked(
                              X11GetConn(), X11GetRoot(), XCB_CW_EVENT_MASK,
                              (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                           XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE})))
    {
        ASSERT(false && "another wm is already running");
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
            bool skip = attrReply->override_redirect || attrReply->map_state == XCB_MAP_STATE_UNMAPPED;
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

    grabKey(1, 0, 0, 0, KeyPhysical::W);
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

    X11Flush();
    X11Ungrab();
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

    WmInit();

    bool isRunning = true;
    while (isRunning && !xcb_connection_has_error(X11GetConn()))
    {
        pollfd fd{
            .fd = xcb_get_file_descriptor(X11GetConn()),
            .events = POLLIN,
        };
        if (poll(&fd, 1, -1) == -1)
        {
            LOG("poll(): %s", strerror(errno));
            continue;
        }
        if (fd.revents & POLLIN)
        {
            WmProcess(isRunning);
            X11Flush();
        }
    }

    LOG("exiting");
}

} // namespace nyla