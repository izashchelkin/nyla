// infinite strip tiling window manager for X11

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/binary_search.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
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
#include "nyla/commons/span.h" // IWYU pragma: keep
#include "nyla/commons/time.h"
#include "nyla/commons/word.h"
#include "nyla/commons/x11_wm_hints_linux.h"

#include <unistd.h>
#include <xcb/randr.h>

namespace nyla
{

namespace
{

static constexpr uint32_t kBaseWindowWidth = 1280;
static constexpr const char *kStateFilePath = "/tmp/nyla_wm_state";
static constexpr uint32_t kStateMagic = DWordBE("NYLA");

// ─── Font data ───
// 8x16 monospace glyphs, 96 chars (0x20-0x7F).
// clang-format off
static constexpr uint32_t kFontData[96][4] = {
    {0x00000000, 0x00000000, 0x00000000, 0x00000000},
    {0x00001010, 0x10101010, 0x10001010, 0x00000000},
    {0x00242424, 0x00000000, 0x00000000, 0x00000000},
    {0x00002424, 0x247E2424, 0x7E242424, 0x00000000},
    {0x0010107C, 0x9290907C, 0x1212927C, 0x10100000},
    {0x00006494, 0x68081010, 0x202C524C, 0x00000000},
    {0x00001824, 0x2418304A, 0x4444443A, 0x00000000},
    {0x00101010, 0x00000000, 0x00000000, 0x00000000},
    {0x00000810, 0x20202020, 0x20201008, 0x00000000},
    {0x00002010, 0x08080808, 0x08081020, 0x00000000},
    {0x00000000, 0x0024187E, 0x18240000, 0x00000000},
    {0x00000000, 0x0010107C, 0x10100000, 0x00000000},
    {0x00000000, 0x00000000, 0x00001010, 0x20000000},
    {0x00000000, 0x0000007E, 0x00000000, 0x00000000},
    {0x00000000, 0x00000000, 0x00001010, 0x00000000},
    {0x00000404, 0x08081010, 0x20204040, 0x00000000},
    {0x00003C42, 0x42464A52, 0x6242423C, 0x00000000},
    {0x00000818, 0x28080808, 0x0808083E, 0x00000000},
    {0x00003C42, 0x42020408, 0x1020407E, 0x00000000},
    {0x00003C42, 0x42021C02, 0x0242423C, 0x00000000},
    {0x00000206, 0x0A122242, 0x7E020202, 0x00000000},
    {0x00007E40, 0x40407C02, 0x0202423C, 0x00000000},
    {0x00001C20, 0x40407C42, 0x4242423C, 0x00000000},
    {0x00007E02, 0x02040408, 0x08101010, 0x00000000},
    {0x00003C42, 0x42423C42, 0x4242423C, 0x00000000},
    {0x00003C42, 0x4242423E, 0x02020438, 0x00000000},
    {0x00000000, 0x00101000, 0x00001010, 0x00000000},
    {0x00000000, 0x00101000, 0x00001010, 0x20000000},
    {0x00000004, 0x08102040, 0x20100804, 0x00000000},
    {0x00000000, 0x007E0000, 0x7E000000, 0x00000000},
    {0x00000040, 0x20100804, 0x08102040, 0x00000000},
    {0x00003C42, 0x42420408, 0x08000808, 0x00000000},
    {0x00007C82, 0x9EA2A2A2, 0xA69A807E, 0x00000000},
    {0x00003C42, 0x4242427E, 0x42424242, 0x00000000},
    {0x00007C42, 0x42427C42, 0x4242427C, 0x00000000},
    {0x00003C42, 0x42404040, 0x4042423C, 0x00000000},
    {0x00007844, 0x42424242, 0x42424478, 0x00000000},
    {0x00007E40, 0x40407840, 0x4040407E, 0x00000000},
    {0x00007E40, 0x40407840, 0x40404040, 0x00000000},
    {0x00003C42, 0x4240404E, 0x4242423C, 0x00000000},
    {0x00004242, 0x42427E42, 0x42424242, 0x00000000},
    {0x00003810, 0x10101010, 0x10101038, 0x00000000},
    {0x00000E04, 0x04040404, 0x04444438, 0x00000000},
    {0x00004244, 0x48506060, 0x50484442, 0x00000000},
    {0x00004040, 0x40404040, 0x4040407E, 0x00000000},
    {0x000082C6, 0xAA928282, 0x82828282, 0x00000000},
    {0x00004242, 0x4262524A, 0x46424242, 0x00000000},
    {0x00003C42, 0x42424242, 0x4242423C, 0x00000000},
    {0x00007C42, 0x4242427C, 0x40404040, 0x00000000},
    {0x00003C42, 0x42424242, 0x42424A3C, 0x02000000},
    {0x00007C42, 0x4242427C, 0x50484442, 0x00000000},
    {0x00003C42, 0x40403C02, 0x0242423C, 0x00000000},
    {0x0000FE10, 0x10101010, 0x10101010, 0x00000000},
    {0x00004242, 0x42424242, 0x4242423C, 0x00000000},
    {0x00004242, 0x42424224, 0x24241818, 0x00000000},
    {0x00008282, 0x82828282, 0x92AAC682, 0x00000000},
    {0x00004242, 0x24241818, 0x24244242, 0x00000000},
    {0x00008282, 0x44442810, 0x10101010, 0x00000000},
    {0x00007E02, 0x02040810, 0x2040407E, 0x00000000},
    {0x00003820, 0x20202020, 0x20202038, 0x00000000},
    {0x00004040, 0x20201010, 0x08080404, 0x00000000},
    {0x00003808, 0x08080808, 0x08080838, 0x00000000},
    {0x00102844, 0x00000000, 0x00000000, 0x00000000},
    {0x00000000, 0x00000000, 0x00000000, 0x007E0000},
    {0x10080000, 0x00000000, 0x00000000, 0x00000000},
    {0x00000000, 0x003C023E, 0x4242423E, 0x00000000},
    {0x00004040, 0x407C4242, 0x4242427C, 0x00000000},
    {0x00000000, 0x003C4240, 0x4040423C, 0x00000000},
    {0x00000202, 0x023E4242, 0x4242423E, 0x00000000},
    {0x00000000, 0x003C4242, 0x7E40403C, 0x00000000},
    {0x00000E10, 0x107C1010, 0x10101010, 0x00000000},
    {0x00000000, 0x003E4242, 0x4242423E, 0x02023C00},
    {0x00004040, 0x407C4242, 0x42424242, 0x00000000},
    {0x00001010, 0x00301010, 0x10101038, 0x00000000},
    {0x00000404, 0x000C0404, 0x04040404, 0x44443800},
    {0x00004040, 0x40424448, 0x70484442, 0x00000000},
    {0x00003010, 0x10101010, 0x10101038, 0x00000000},
    {0x00000000, 0x00FC9292, 0x92929292, 0x00000000},
    {0x00000000, 0x007C4242, 0x42424242, 0x00000000},
    {0x00000000, 0x003C4242, 0x4242423C, 0x00000000},
    {0x00000000, 0x007C4242, 0x4242427C, 0x40404000},
    {0x00000000, 0x003E4242, 0x4242423E, 0x02020200},
    {0x00000000, 0x005E6040, 0x40404040, 0x00000000},
    {0x00000000, 0x003E4040, 0x3C02027C, 0x00000000},
    {0x00001010, 0x107C1010, 0x1010100E, 0x00000000},
    {0x00000000, 0x00424242, 0x4242423E, 0x00000000},
    {0x00000000, 0x00424242, 0x24241818, 0x00000000},
    {0x00000000, 0x00828292, 0x9292927C, 0x00000000},
    {0x00000000, 0x00424224, 0x18244242, 0x00000000},
    {0x00000000, 0x00424242, 0x4242423E, 0x02023C00},
    {0x00000000, 0x007E0408, 0x1020407E, 0x00000000},
    {0x00000C10, 0x10102010, 0x1010100C, 0x00000000},
    {0x00001010, 0x10101010, 0x10101010, 0x00000000},
    {0x00003008, 0x08080408, 0x08080830, 0x00000000},
    {0x0062928C, 0x00000000, 0x00000000, 0x00000000},
    {0x08100000, 0x00000000, 0x00000000, 0x00000000},
};
// clang-format on

static constexpr uint32_t kGlyphWidth = 8;
static constexpr uint32_t kGlyphHeight = 16;

struct Rect
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

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

// _NET_WM_MOVERESIZE direction values we handle.
static constexpr uint32_t kMoveresizeSizeRight = 3;
static constexpr uint32_t kMoveresizeSizeLeft = 7;
static constexpr uint32_t kMoveresizeCancel = 11;

auto IsHorizontalResize(uint32_t direction) -> bool
{
    return direction != 1     // SizeTop
           && direction != 5  // SizeBottom
           && direction != 8  // Move
           && direction <= 7; // not MoveKeyboard(10) or Cancel(11)
}

auto IsFromLeft(uint32_t direction) -> bool
{
    return direction == 7     // SizeLeft
           || direction == 0  // SizeTopLeft
           || direction == 6; // SizeBottomLeft
}

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
static constexpr uint32_t kTierCount = 4;

struct window_manager
{
    uint8_t activeStackIndex;
    bool layoutDirty;
    bool borderDirty;
    bool barDirty;
    bool follow;
    bool focusCheckPending;

    uint32_t barHeight;
    int32_t monitorX;
    int32_t monitorY;
    uint32_t monitorWidth;
    uint32_t monitorHeight;
    xcb_window_t lastEnteredWindow;
    xcb_get_input_focus_cookie_t focusCheckCookie;

    bool moveresizeActive;
    xcb_window_t moveresizeWindow;
    int32_t moveresizeOrigPointerX;
    uint32_t moveresizeOrigWidth;
    uint32_t moveresizeDirection;

    array<window_stack, 9> stacks;
    inline_vec<xcb_window_t, 64> pendingClients;
    array<uint32_t, kTierCount> tiers;

    // ─── Bar rendering ───
    xcb_window_t barWindow;
    xcb_pixmap_t barPixmap;
    xcb_gcontext_t barGC;

    span<uint8_t> memory;
    uint32_t capacity;
    uint32_t windowCount;
    window_index_entry *index;
    window_data_entry *data;
};

window_manager *wm;

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

auto DataBase(window_index_entry *idx, uint32_t cap) -> window_data_entry *
{
    return (window_data_entry *)(idx + cap);
}

void ManageClient(xcb_window_t clientWindow)
{
    if (FindIndex(clientWindow))
        return;

    if (wm->windowCount >= wm->capacity)
    {
        uint32_t newCap = wm->capacity * 2;
        window_data_entry *newData = DataBase(wm->index, newCap);
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

    uint32_t wmEventMask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW |
                           XCB_EVENT_MASK_LEAVE_WINDOW;
    xcb_change_window_attributes(X11GetConn(), clientWindow, XCB_CW_EVENT_MASK, &wmEventMask);

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
        wm->barDirty = true;
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
    wm->barDirty = true;
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

void RecalcTiers()
{
    uint32_t viewW = wm->monitorWidth;
    uint32_t base = kBaseWindowWidth;
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

// -- RandR output selection and CRTC configuration --

auto IsExternalOutputName(const char *name, uint8_t nameLen) -> bool
{
    if (nameLen >= 3 && (name[0] == 'e' || name[0] == 'E') && (name[1] == 'D' || name[1] == 'd') &&
        (name[2] == 'P' || name[2] == 'p'))
        return false;
    if (nameLen >= 4 && (name[0] == 'L' || name[0] == 'l') && (name[1] == 'V' || name[1] == 'v') &&
        (name[2] == 'D' || name[2] == 'd') && (name[3] == 'S' || name[3] == 's'))
        return false;
    return true;
}

auto PickBestMode(xcb_randr_get_output_info_reply_t *infoReply, xcb_randr_mode_info_t *resourceModes,
                  int numResourceModes) -> xcb_randr_mode_t
{
    xcb_randr_mode_t *outputModes = xcb_randr_get_output_info_modes(infoReply);
    int numModes = infoReply->num_modes;
    xcb_randr_mode_t bestMode = XCB_NONE;
    uint64_t bestPixels = 0;
    float bestRefresh = 0.0f;
    for (int mi = 0; mi < numModes; ++mi)
    {
        xcb_randr_mode_t modeId = outputModes[mi];
        xcb_randr_mode_info_t *modeInfo = nullptr;
        for (int ri = 0; ri < numResourceModes; ++ri)
        {
            if (resourceModes[ri].id == modeId)
            {
                modeInfo = &resourceModes[ri];
                break;
            }
        }
        if (!modeInfo)
            continue;
        uint64_t pixels = (uint64_t)modeInfo->width * (uint64_t)modeInfo->height;
        float refresh = 0.0f;
        if (modeInfo->htotal > 0 && modeInfo->vtotal > 0)
            refresh = (float)modeInfo->dot_clock / ((float)modeInfo->htotal * (float)modeInfo->vtotal);
        if (pixels > bestPixels || (pixels == bestPixels && refresh > bestRefresh))
        {
            bestPixels = pixels;
            bestRefresh = refresh;
            bestMode = modeId;
        }
    }
    return bestMode != XCB_NONE ? bestMode : *outputModes;
}

// Pick the best output (external preferred) and enable only it.
// Returns true if the active monitor geometry changed.
auto RandRRefresh() -> bool
{
    if (X11RandRGetEventOffset() == 0)
        return false;

    xcb_randr_get_screen_resources_cookie_t resCookie = xcb_randr_get_screen_resources(X11GetConn(), X11GetRoot());
    xcb_randr_get_screen_resources_reply_t *resReply =
        xcb_randr_get_screen_resources_reply(X11GetConn(), resCookie, nullptr);
    if (!resReply)
        return false;

    int numOutputs = xcb_randr_get_screen_resources_outputs_length(resReply);
    xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_outputs(resReply);
    int numResourceModes = xcb_randr_get_screen_resources_modes_length(resReply);
    xcb_randr_mode_info_t *resourceModes = xcb_randr_get_screen_resources_modes(resReply);

    xcb_randr_output_t bestOutput = XCB_NONE;
    xcb_randr_mode_t bestMode = XCB_NONE;
    for (int pass = 0; pass < 2; ++pass)
    {
        bool wantExternal = (pass == 0);
        for (int i = 0; i < numOutputs; ++i)
        {
            xcb_randr_get_output_info_cookie_t ic =
                xcb_randr_get_output_info(X11GetConn(), outputs[i], XCB_CURRENT_TIME);
            xcb_randr_get_output_info_reply_t *ir = xcb_randr_get_output_info_reply(X11GetConn(), ic, nullptr);
            if (!ir)
                continue;
            bool ext = IsExternalOutputName((const char *)xcb_randr_get_output_info_name(ir),
                                            xcb_randr_get_output_info_name_length(ir));
            bool ok = (ir->connection == XCB_RANDR_CONNECTION_CONNECTED) && wantExternal == ext && ir->num_modes > 0;
            if (ok)
            {
                bestOutput = outputs[i];
                bestMode = PickBestMode(ir, resourceModes, numResourceModes);
            }
            free(ir);
            if (bestOutput != XCB_NONE)
                break;
        }
        if (bestOutput != XCB_NONE)
            break;
    }
    free(resReply);
    if (bestOutput == XCB_NONE)
        return false;

    xcb_randr_get_screen_resources_cookie_t resCookie2 = xcb_randr_get_screen_resources(X11GetConn(), X11GetRoot());
    xcb_randr_get_screen_resources_reply_t *resReply2 =
        xcb_randr_get_screen_resources_reply(X11GetConn(), resCookie2, nullptr);
    if (!resReply2)
        return false;
    int numOutputs2 = xcb_randr_get_screen_resources_outputs_length(resReply2);
    xcb_randr_output_t *outputs2 = xcb_randr_get_screen_resources_outputs(resReply2);

    for (int i = 0; i < numOutputs2; ++i)
    {
        if (outputs2[i] == bestOutput)
            continue;
        xcb_randr_get_output_info_cookie_t ic = xcb_randr_get_output_info(X11GetConn(), outputs2[i], XCB_CURRENT_TIME);
        xcb_randr_get_output_info_reply_t *ir = xcb_randr_get_output_info_reply(X11GetConn(), ic, nullptr);
        if (ir)
        {
            if (ir->crtc != XCB_NONE)
                xcb_randr_set_crtc_config(X11GetConn(), ir->crtc, XCB_CURRENT_TIME, resReply2->config_timestamp, 0, 0,
                                          XCB_NONE, XCB_RANDR_ROTATION_ROTATE_0, 0, nullptr);
            free(ir);
        }
    }

    // Fresh timestamp after disable loop -- each set_crtc_config invalidates the old one.
    xcb_randr_get_screen_resources_cookie_t resCookie3 = xcb_randr_get_screen_resources(X11GetConn(), X11GetRoot());
    xcb_randr_get_screen_resources_reply_t *resReply3 =
        xcb_randr_get_screen_resources_reply(X11GetConn(), resCookie3, nullptr);
    if (!resReply3)
    {
        free(resReply2);
        return false;
    }
    free(resReply2);

    xcb_randr_get_output_info_cookie_t bic = xcb_randr_get_output_info(X11GetConn(), bestOutput, XCB_CURRENT_TIME);
    xcb_randr_get_output_info_reply_t *bir = xcb_randr_get_output_info_reply(X11GetConn(), bic, nullptr);
    if (bir)
    {
        xcb_randr_crtc_t useCrtc = bir->crtc;
        if (useCrtc == XCB_NONE)
        {
            int numCrtcs = xcb_randr_get_screen_resources_crtcs_length(resReply3);
            xcb_randr_crtc_t *crtcs = xcb_randr_get_screen_resources_crtcs(resReply3);
            for (int c = 0; c < numCrtcs; ++c)
            {
                xcb_randr_get_crtc_info_cookie_t cc = xcb_randr_get_crtc_info(X11GetConn(), crtcs[c], XCB_CURRENT_TIME);
                xcb_randr_get_crtc_info_reply_t *cr = xcb_randr_get_crtc_info_reply(X11GetConn(), cc, nullptr);
                if (cr)
                {
                    if (xcb_randr_get_crtc_info_outputs_length(cr) == 0)
                    {
                        useCrtc = crtcs[c];
                        free(cr);
                        break;
                    }
                    free(cr);
                }
            }
        }
        if (useCrtc != XCB_NONE && bestMode != XCB_NONE)
            xcb_randr_set_crtc_config(X11GetConn(), useCrtc, XCB_CURRENT_TIME, resReply3->config_timestamp, 0, 0,
                                      bestMode, XCB_RANDR_ROTATION_ROTATE_0, 1, &bestOutput);
        free(bir);
    }
    X11Flush();

    xcb_randr_get_output_info_cookie_t fic = xcb_randr_get_output_info(X11GetConn(), bestOutput, XCB_CURRENT_TIME);
    xcb_randr_get_output_info_reply_t *fir = xcb_randr_get_output_info_reply(X11GetConn(), fic, nullptr);
    bool changed = false;
    if (fir && fir->crtc != XCB_NONE)
    {
        xcb_randr_get_crtc_info_cookie_t cc = xcb_randr_get_crtc_info(X11GetConn(), fir->crtc, XCB_CURRENT_TIME);
        xcb_randr_get_crtc_info_reply_t *cr = xcb_randr_get_crtc_info_reply(X11GetConn(), cc, nullptr);
        if (cr)
        {
            int32_t nx = cr->x, ny = cr->y;
            uint32_t nw = cr->width, nh = cr->height;
            free(cr);
            if (nx != wm->monitorX || ny != wm->monitorY || nw != wm->monitorWidth || nh != wm->monitorHeight)
            {
                wm->monitorX = nx;
                wm->monitorY = ny;
                wm->monitorWidth = nw;
                wm->monitorHeight = nh;
                changed = true;
            }
        }
    }
    if (fir)
        free(fir);
    free(resReply3);
    return changed;
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
void WmShutdown(bool &isRunning)
{
    if (wm->moveresizeActive)
    {
        xcb_ungrab_pointer(X11GetConn(), XCB_CURRENT_TIME);
        wm->moveresizeActive = false;
        wm->moveresizeWindow = XCB_NONE;
    }

    X11Grab();
    X11Flush();

    {
        uint8_t buf[4096];
        // Serialization format -- see WmInit for deserialization
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
            writeU8((uint8_t)(stk.flags & 0xFF));
            writeU8((uint8_t)(stk.flags >> 8));
            for (uint64_t j = 0; j < stk.windows.size; ++j)
            {
                xcb_window_t w = stk.windows[j];
                window_index_entry *ix = FindIndex(w);
                writeU32((uint32_t)w);
                writeU8(ix ? ix->dataEntry->tierIndex : 1);
                writeU8(w == stk.activeWindow ? 1 : 0);
            }
            writeU32(0);
        }
        file_handle f = FileOpen({(uint8_t *)kStateFilePath, CStrLen(kStateFilePath, 32)}, FileOpenMode::Write);
        if (FileValid(f))
        {
            FileWrite(f, pos, buf);
            FileClose(f);
        }
    }

    X11Ungrab();
    X11Flush();
    xcb_disconnect(X11GetConn());
    isRunning = false;
}

// ─── Bar rendering ───

auto BarRedraw() -> void
{
    uint32_t barW = wm->monitorWidth;
    uint32_t barH = wm->barHeight;

    // Clear to background — match border color in follow mode
    {
        uint32_t bgColor = wm->follow ? (uint32_t)Color::KActiveFollow : 0x1A1A1A;
        xcb_change_gc(X11GetConn(), wm->barGC, XCB_GC_FOREGROUND, &bgColor);
        xcb_rectangle_t bgRect = {0, 0, (uint16_t)barW, (uint16_t)barH};
        xcb_poly_fill_rectangle(X11GetConn(), wm->barPixmap, wm->barGC, 1, &bgRect);
    }

    // Build text line
    uint8_t lineBuf[512];
    int textLen = 0;
    {
        wall_clock_time wc = GetWallClockTime();
        const window_stack &activeStack = GetActiveStack();
        if (activeStack.activeWindow)
        {
            window_index_entry *activeIdx = FindIndex(activeStack.activeWindow);
            if (activeIdx && activeIdx->dataEntry->name.size > 0)
            {
                textLen = (int)StringWriteFmt({lineBuf, sizeof(lineBuf)}, "%02d:%02d:%02d %02d.%02d.%04d  %.*s [%u]"_s,
                                              wc.hour, wc.minute, wc.second, wc.day, wc.month, wc.year,
                                              activeIdx->dataEntry->name.size, activeIdx->dataEntry->name.data.data,
                                              (uint32_t)wm->activeStackIndex + 1);
            }
            else
            {
                textLen = (int)StringWriteFmt({lineBuf, sizeof(lineBuf)}, "%02d:%02d:%02d %02d.%02d.%04d"_s, wc.hour,
                                              wc.minute, wc.second, wc.day, wc.month, wc.year);
            }
        }
        else
        {
            textLen = (int)StringWriteFmt({lineBuf, sizeof(lineBuf)}, "%02d:%02d:%02d %02d.%02d.%04d"_s, wc.hour,
                                          wc.minute, wc.second, wc.day, wc.month, wc.year);
        }
    }

    // Set foreground color (light gray) and draw glyphs via batched points
    if (textLen > 0)
    {
        uint32_t fgColor = wm->follow ? 0x1A1A1A : 0xC8C8C8;
        xcb_change_gc(X11GetConn(), wm->barGC, XCB_GC_FOREGROUND, &fgColor);

        xcb_point_t points[256];
        uint32_t pointCount = 0;
        auto flush = [&] {
            if (pointCount == 0)
                return;
            xcb_poly_point(X11GetConn(), XCB_COORD_MODE_ORIGIN, wm->barPixmap, wm->barGC, pointCount, points);
            pointCount = 0;
        };

        uint32_t startX = 4;
        for (int ci = 0; ci < textLen; ++ci)
        {
            uint8_t ch = (uint8_t)lineBuf[ci];
            if (ch < 0x20 || ch > 0x7F)
                continue;
            uint32_t glyphIdx = ch - 0x20;
            uint32_t baseX = startX + (uint32_t)ci * kGlyphWidth;
            for (uint32_t gy = 0; gy < kGlyphHeight && gy < barH; ++gy)
            {
                uint32_t rowGroup = gy / 4;
                uint32_t rowIdx = 3 - (gy % 4);
                uint32_t rowByte = (kFontData[glyphIdx][rowGroup] >> (8 * rowIdx)) & 0xFF;
                for (uint32_t gx = 0; gx < kGlyphWidth; ++gx)
                {
                    if ((rowByte >> (7 - gx)) & 1)
                    {
                        points[pointCount++] = {(int16_t)(baseX + gx), (int16_t)(gy + 2)};
                        if (pointCount == 256)
                            flush();
                    }
                }
            }
        }
        flush();
    }

    xcb_copy_area(X11GetConn(), wm->barPixmap, wm->barWindow, wm->barGC, 0, 0, 0, 0, (uint16_t)barW, (uint16_t)barH);
    X11Flush();
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
                WmShutdown(isRunning);
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
                wm->barDirty = true;
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
                        if (direction == kMoveresizeCancel)
                            break;

                        if (IsHorizontalResize((uint32_t)direction))
                        {
                            wm->moveresizeActive = true;
                            wm->moveresizeWindow = clientWin;
                            wm->moveresizeDirection = (uint32_t)direction;
                            wm->moveresizeOrigWidth = TierWidth(idx->dataEntry->tierIndex);
                            wm->moveresizeOrigPointerX = cm->data.data32[0];

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
                bool fromLeft = IsFromLeft(wm->moveresizeDirection);
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

        case XCB_EXPOSE: {
            auto *exp = reinterpret_cast<xcb_expose_event_t *>(event);
            if (exp->window == wm->barWindow && exp->count == 0)
                BarRedraw();
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
                if (RandRRefresh())
                {
                    RecalcTiers();
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
                                goto focus_check_done; // skip post-walk: foreign window
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
    inline_vec<xcb_window_t, 16> inputOnlyWindows{};
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
                if (idx.window == GetActiveStack().activeWindow)
                    wm->barDirty = true;
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

        if (data.pendingAttrCookie.sequence)
        {
            xcb_get_window_attributes_reply_t *ar =
                xcb_get_window_attributes_reply(X11GetConn(), data.pendingAttrCookie, nullptr);
            data.pendingAttrCookie.sequence = 0;
            if (ar && ar->_class == XCB_WINDOW_CLASS_INPUT_ONLY)
                InlineVec::Append(inputOnlyWindows, data.window);
            free(ar);
        }
    }

    for (uint64_t i = 0; i < inputOnlyWindows.size; ++i)
        UnmanageClient(inputOnlyWindows[i]);

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
        Rect screenRect = {wm->monitorX, wm->monitorY, wm->monitorWidth, wm->monitorHeight};
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
            int32_t xc = 0;
            for (uint32_t j = 0; j < n; ++j)
            {
                xcb_window_t cw = stack.windows[j];
                window_index_entry *cix = FindIndex(cw);
                uint32_t w = cix ? TierWidth(cix->dataEntry->tierIndex) : kBaseWindowWidth;
                uint32_t capW = Min(w, (uint32_t)viewW);
                if (cix && cix->dataEntry->maxWidth)
                    capW = Min(capW, cix->dataEntry->maxWidth);
                capW = Max(capW, 100u);
                InlineVec::Append(winX, xc);
                InlineVec::Append(winW, capW);
                xc += (int32_t)capW;
            }

            int32_t contentEnd = InlineVec::Back(winX) + (int32_t)InlineVec::Back(winW);

            // When center-scroll is on and the strip fits, center the whole
            // strip. Otherwise offset by 40px for the leftmost gap.
            if ((stack.flags & window_stack::FlagCenterScroll) && contentEnd <= viewW)
            {
                int32_t centerOffset = (viewW - contentEnd) / 2;
                for (uint32_t i = 0; i < n; ++i)
                    winX[i] += centerOffset;
                contentEnd += centerOffset;
            }
            else
            {
                for (uint32_t i = 0; i < n; ++i)
                    winX[i] += 20;
                contentEnd += 20;
            }

            int32_t winLeft = winX[activeIndex];
            int32_t winRight = winLeft + (int32_t)winW[activeIndex];

            int32_t gap = 20;
            int32_t maxScroll = Max(0, contentEnd + gap - viewW);
            int32_t scroll = stack.scrollOffset;

            if (stack.flags & window_stack::FlagCenterScroll)
            {
                scroll = winLeft + (int32_t)winW[activeIndex] / 2 - viewW / 2;
            }
            else
            {
                int32_t lo = winRight - (viewW - gap);
                int32_t hi = winLeft - gap;
                if (lo < hi)
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

    if (wm->barDirty)
    {
        wm->barDirty = false;
        BarRedraw();
    }
}

// -- init --

} // namespace

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
    wm->data = DataBase(wm->index, wm->capacity);
    CommitMemPages(wm->memory.data, (uint8_t *)(wm->data + wm->capacity) - wm->memory.data);

    wm->moveresizeActive = false;
    wm->barHeight = 20;
    wm->monitorX = 0;
    wm->monitorY = 0;
    wm->monitorWidth = X11GetScreen()->width_in_pixels;
    wm->monitorHeight = X11GetScreen()->height_in_pixels;
    wm->barDirty = true;
    for (auto &s : wm->stacks)
        s.flags |= window_stack::FlagCenterScroll;

    if (xcb_request_check(X11GetConn(),
                          xcb_change_window_attributes_checked(
                              X11GetConn(), X11GetRoot(), XCB_CW_EVENT_MASK,
                              (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                           XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE})))
    {
        ASSERT(false && "another wm is already running");
    }

    // EWMH _NET_SUPPORTING_WM_CHECK detection protocol
    {
        xcb_window_t checkWin = xcb_generate_id(X11GetConn());
        xcb_create_window(X11GetConn(), XCB_COPY_FROM_PARENT, checkWin, X11GetRoot(), 0, 0, 1, 1, 0,
                          XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0, nullptr);
        xcb_map_window(X11GetConn(), checkWin);

        static const char k_WMName[] = "nyla-wm";
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, checkWin, X11GetAtoms().net_wm_name,
                            X11GetAtoms().utf8_string, 8, sizeof(k_WMName) - 1, k_WMName);

        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, checkWin, X11GetAtoms().net_supporting_wm_check,
                            XCB_ATOM_WINDOW, 32, 1, &checkWin);
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, X11GetRoot(), X11GetAtoms().net_supporting_wm_check,
                            XCB_ATOM_WINDOW, 32, 1, &checkWin);
    }

    // Advertise EWMH features
    {
        const xcb_atom_t supportedAtoms[] = {X11GetAtoms().net_wm_moveresize};
        xcb_change_property(X11GetConn(), XCB_PROP_MODE_REPLACE, X11GetRoot(), X11GetAtoms().net_supported,
                            XCB_ATOM_ATOM, 32, sizeof(supportedAtoms) / sizeof(xcb_atom_t), supportedAtoms);
    }

    RandRRefresh();
    RecalcTiers();

    X11Grab();
    X11Flush();
    {
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

        if (wm->pendingClients.size > 0)
        {
            for (uint64_t i = 0; i < wm->pendingClients.size; ++i)
            {
                xcb_window_t cw = wm->pendingClients[i];
                window_index_entry *idx = FindIndex(cw);
                if (!idx || !idx->parent)
                    continue;
                for (int j = 0; j < 10; ++j)
                {
                    window_index_entry *p = FindIndex(idx->parent);
                    if (!p || !p->parent)
                        break;
                    idx->parent = p->parent;
                }
                if (!FindIndex(idx->parent))
                    idx->flags |= window_index_entry::Flag_PendingParent;
            }

            bool activated = false;
            for (uint64_t i = 0; i < wm->pendingClients.size; ++i)
            {
                xcb_window_t cw = wm->pendingClients[i];
                window_index_entry *idx = FindIndex(cw);
                if (!idx)
                    continue;
                bool knownTransient = idx->parent && !(idx->flags & window_index_entry::Flag_PendingParent);
                window_stack *ts = &wm->stacks[wm->activeStackIndex];
                if (knownTransient)
                {
                    for (uint32_t s = 0; s < 9; ++s)
                    {
                        if (InlineVec::Find(wm->stacks[s].windows, idx->parent))
                        {
                            ts = &wm->stacks[s];
                            break;
                        }
                    }
                }
                InlineVec::Append(ts->windows, cw);
                if (!activated)
                {
                    ts->activeWindow = cw;
                    activated = true;
                }
            }
            InlineVec::Clear(wm->pendingClients);
        }

        {
            file_handle f = FileOpen({(uint8_t *)kStateFilePath, CStrLen(kStateFilePath, 32)}, FileOpenMode::Read);
            if (FileValid(f))
            {
                uint8_t buf[4096];
                uint32_t len = FileRead(f, 4096, buf);
                FileClose(f);
                unlink(kStateFilePath);

                if (len >= 6)
                {
                    byte_parser p;
                    ByteParser::Init(p, buf, len);
                    auto read8 = [&]() { return ByteParser::ReadOrDefault(p, 0); };
                    auto read32 = [&]() { return ByteParser::BytesLeft(p) >= 4 ? ByteParser::Read<uint32_t>(p) : 0u; };

                    if (read32() == kStateMagic)
                    {
                        wm->activeStackIndex = read8();
                        if (wm->activeStackIndex < 9)
                        {
                            wm->follow = (read8() != 0);
                            for (int s = 0; s < 9; ++s)
                            {
                                uint8_t fl = read8();
                                uint8_t fh = read8();
                                wm->stacks[s].flags = (uint16_t)fl | ((uint16_t)fh << 8);
                                for (;;)
                                {
                                    xcb_window_t xid = (xcb_window_t)read32();
                                    if (!xid)
                                        break;
                                    uint8_t ti = read8();
                                    bool isActive = (read8() != 0);
                                    window_index_entry *ix = FindIndex(xid);
                                    if (!ix)
                                        continue;
                                    ix->dataEntry->tierIndex = ti;
                                    for (uint32_t cs = 0; cs < 9; ++cs)
                                    {
                                        xcb_window_t *wp = InlineVec::Find(wm->stacks[cs].windows, xid);
                                        if (wp)
                                        {
                                            InlineVec::Erase(wm->stacks[cs].windows, wp);
                                            break;
                                        }
                                    }
                                    InlineVec::Append(wm->stacks[s].windows, xid);
                                    if (isActive)
                                    {
                                        wm->stacks[s].activeWindow = xid;
                                        if (s == wm->activeStackIndex)
                                            Activate(wm->stacks[s], xid, XCB_CURRENT_TIME);
                                    }
                                }
                            }
                        }
                        wm->layoutDirty = true;
                    }
                }
            }
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
    }
    X11Ungrab();
    X11Flush();

    LOG("[wm] init done");

    // ─── Create bar window ───
    {
        uint32_t barW = wm->monitorWidth;
        uint32_t barH = wm->barHeight;
        wm->barWindow = xcb_generate_id(X11GetConn());
        xcb_create_window(X11GetConn(), XCB_COPY_FROM_PARENT, wm->barWindow, X11GetRoot(), 0, 0, (uint16_t)barW,
                          (uint16_t)barH, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, X11GetScreen()->root_visual,
                          XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, (uint32_t[]){1, XCB_EVENT_MASK_EXPOSURE});
        xcb_configure_window(X11GetConn(), wm->barWindow, XCB_CONFIG_WINDOW_STACK_MODE,
                             (uint32_t[]){XCB_STACK_MODE_BELOW});
        xcb_map_window(X11GetConn(), wm->barWindow);

        wm->barPixmap = xcb_generate_id(X11GetConn());
        xcb_create_pixmap(X11GetConn(), X11GetScreen()->root_depth, wm->barPixmap, X11GetRoot(), (uint16_t)barW,
                          (uint16_t)barH);
        wm->barGC = xcb_generate_id(X11GetConn());
        xcb_create_gc(X11GetConn(), wm->barGC, wm->barPixmap, 0, nullptr);
    }

    // Launch daemons (best-effort, non-blocking).
    // Suppressed when NYLA_WM_NO_DAEMONS=1 (test harness).
    if (!getenv("NYLA_WM_NO_DAEMONS"))
    {
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
        int pollRes = poll(&fd, 1, 500);
        if (pollRes == -1)
        {
            if (errno == EINTR)
                continue;
            LOG("poll(): %s", strerror(errno));
            continue;
        }
        if (pollRes > 0 && (fd.revents & POLLIN))
        {
            WmProcess(isRunning);
        }
        // Redraw bar periodically for clock
        static uint64_t lastBarTime = 0;
        uint64_t now = GetMonotonicTimeMillis();
        if (now - lastBarTime >= 500)
        {
            lastBarTime = now;
            BarRedraw();
        }
        X11Flush();
    }

    LOG("exiting");
}

} // namespace nyla