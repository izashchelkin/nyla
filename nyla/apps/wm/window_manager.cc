#include "nyla/apps/wm/window_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "nyla/apps/wm/layout.h"
#include "nyla/apps/wm/palette.h"
#include "nyla/apps/wm/screen_saver_inhibitor.h"
#include "nyla/apps/wm/wm_background.h"
#include "nyla/commons/containers/map.h"
#include "nyla/commons/future.h"
#include "nyla/debugfs/debugfs.h"
#include "nyla/platform/x11/platform_x11.h"
#include "nyla/platform/x11/platform_x11_error.h"
#include "nyla/platform/x11/platform_x11_wm_hints.h"
#include "xcb/xcb.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"

namespace nyla
{

using namespace platform_x11_internal;

static auto DumpClients() -> std::string;

struct WindowStack
{
    LayoutType layoutType;
    bool zoom;

    std::vector<xcb_window_t> windows;
    xcb_window_t activeWindow;
};

struct Client
{
    Rect rect;
    uint32_t borderWidth;
    std::string name;

    bool wmHintsInput;
    bool wmTakeFocus;
    bool wmDeleteWindow;

    uint32_t maxWidth;
    uint32_t maxHeight;

    bool urgent;
    bool wantsConfigureNotify;

    xcb_window_t transientFor;
    std::vector<xcb_window_t> subwindows;

    Map<xcb_atom_t, xcb_get_property_cookie_t> propertyCookies;
};

template <typename Sink> void AbslStringify(Sink &sink, const Client &c)
{
    absl::Format(&sink, "Window{ rect=%v, input=%v, take_focus=%v }", c.rect, c.wmHintsInput, c.wmTakeFocus);
}

static uint32_t wmBarHeight = 20;
bool wmBackgroundDirty;

static bool wmLayoutDirty;
static bool wmFollow;
static bool wmBorderDirty;

static Map<xcb_window_t, Client> wmClients;
static std::vector<xcb_window_t> wmPendingClients;

static Map<xcb_atom_t, void (*)(xcb_window_t, Client &, xcb_get_property_reply_t *)> wmPropertyChangeHandlers;

static std::vector<WindowStack> wmStacks;
static uint64_t wmActiveStackIdx;

static xcb_timestamp_t lastRawmotionTs = 0;
static xcb_window_t lastEnteredWindow = 0;

//

static auto GetActiveStack() -> WindowStack &
{
    CHECK_LT(wmActiveStackIdx & 0xFF, wmStacks.size());
    return wmStacks.at(wmActiveStackIdx & 0xFF);
}

static void HandleWmHints(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    WmHints wmHints = [&reply] -> WmHints {
        if (!reply || xcb_get_property_value_length(reply) != sizeof(WmHints))
            return WmHints{};

        return *static_cast<WmHints *>(xcb_get_property_value(reply));
    }();

    Initialize(wmHints);

    client.wmHintsInput = wmHints.input;

    // if (wm_hints.urgent() && !client.urgent) indicator?
    client.urgent = wmHints.Urgent();
}

static void HandleWmNormalHints(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    WmNormalHints wmNormalHints = [&reply] -> WmNormalHints {
        if (!reply || xcb_get_property_value_length(reply) != sizeof(WmNormalHints))
            return WmNormalHints{};

        return *static_cast<WmNormalHints *>(xcb_get_property_value(reply));
    }();

    Initialize(wmNormalHints);
    // LOG(INFO) << client_window << " " << wm_normal_hints;

    client.maxWidth = wmNormalHints.maxWidth;
    client.maxHeight = wmNormalHints.maxHeight;
}

static void HandleWmName(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    if (!reply)
    {
        LOG(ERROR) << "property fetch error";
        return;
    }

    client.name = {static_cast<char *>(xcb_get_property_value(reply)),
                   static_cast<size_t>(xcb_get_property_value_length(reply))};
    // LOG(INFO) << client_window << " name=" << client.name;
}

static void HandleWmProtocols(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    if (!reply)
        return;
    if (reply->type != XCB_ATOM_ATOM)
        return;

    client.wmDeleteWindow = false;
    client.wmTakeFocus = false;

    auto wmProtocols = std::span{
        static_cast<xcb_atom_t *>(xcb_get_property_value(reply)),
        xcb_get_property_value_length(reply) / sizeof(xcb_atom_t),
    };
    // LOG(INFO) << client_window << " " << absl::StrJoin(wm_protocols, ", ");

    for (xcb_atom_t atom : wmProtocols)
    {
        if (atom == x11.atoms.wm_delete_window)
        {
            client.wmDeleteWindow = true;
            continue;
        }
        if (atom == x11.atoms.wm_take_focus)
        {
            client.wmTakeFocus = true;
            continue;
        }
    }
}

static void HandleWmTransientFor(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    if (!reply || !reply->length)
        return;
    if (reply->type != XCB_ATOM_WINDOW)
        return;

    if (client.transientFor != 0)
        return;

    client.transientFor = *reinterpret_cast<xcb_window_t *>(xcb_get_property_value(reply));
}

void InitializeWM()
{
    wmStacks.resize(9);

    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_HINTS, HandleWmHints);
    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_NORMAL_HINTS, HandleWmNormalHints);
    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_NAME, HandleWmName);
    wmPropertyChangeHandlers.try_emplace(x11.atoms.wm_protocols, HandleWmProtocols);
    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_TRANSIENT_FOR, HandleWmTransientFor);

    DebugFsRegister(
        "windows", nullptr,                                       //
        [](auto &file) -> auto { file.content = DumpClients(); }, //
        nullptr);

    ScreenSaverInhibitorInit();
}

static void ClearZoom(WindowStack &stack)
{
    if (!stack.zoom)
        return;

    stack.zoom = false;
    wmLayoutDirty = true;
}

static void ApplyBorder(xcb_connection_t *conn, xcb_window_t window, Color color)
{
    if (!window)
        return;
    xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, &color);
}

static void Activate(const WindowStack &stack, xcb_timestamp_t time)
{
    if (!stack.activeWindow)
    {
        goto revert_to_root;
    }

    if (auto it = wmClients.find(stack.activeWindow); it != wmClients.end())
    {
        wmBorderDirty = true;

        const auto &client = it->second;

        xcb_window_t immediateFocus = client.wmHintsInput ? stack.activeWindow : x11.screen->root;

        xcb_set_input_focus(x11.conn, XCB_INPUT_FOCUS_NONE, immediateFocus, time);

        if (client.wmTakeFocus)
        {
            X11SendWmTakeFocus(stack.activeWindow, time);
        }

        return;
    }

revert_to_root:
    xcb_set_input_focus(x11.conn, XCB_INPUT_FOCUS_NONE, x11.screen->root, time);
    lastEnteredWindow = 0;
}

static void Activate(WindowStack &stack, xcb_window_t clientWindow, xcb_timestamp_t time)
{
    if (stack.activeWindow != clientWindow)
    {
        ApplyBorder(x11.conn, stack.activeWindow, Color::KNone);
        stack.activeWindow = clientWindow;
        wmBackgroundDirty = true;
    }

    Activate(stack, time);
}

static void MaybeActivateUnderPointer(WindowStack &stack, xcb_timestamp_t ts)
{
    if (stack.zoom)
        return;
    if (wmFollow)
        return;

    if (!lastEnteredWindow)
    {
        return;
    }
    if (lastEnteredWindow == x11.screen->root)
    {
        return;
    }
    if (lastEnteredWindow == stack.activeWindow)
    {
        return;
    }

    if (lastRawmotionTs > ts)
        return;
    if (ts - lastRawmotionTs > 3)
        return;

    if (wmClients.find(lastEnteredWindow) != wmClients.end())
    {
        Activate(stack, lastEnteredWindow, ts);
    }
}

static void CheckFocusTheft()
{
    auto reply = xcb_get_input_focus_reply(x11.conn, xcb_get_input_focus(x11.conn), nullptr);
    xcb_window_t focusedWindow = reply->focus;
    free(reply);

    const WindowStack &stack = GetActiveStack();
    if (stack.activeWindow == focusedWindow)
        return;

    if (focusedWindow == x11.screen->root)
        return;
    if (!focusedWindow)
        return;

    if (!wmClients.contains(focusedWindow))
    {
        for (;;)
        {
            xcb_query_tree_reply_t *reply =
                xcb_query_tree_reply(x11.conn, xcb_query_tree(x11.conn, focusedWindow), nullptr);

            if (!reply)
            {
                Activate(stack, XCB_CURRENT_TIME);
                return;
            }

            xcb_window_t parent = reply->parent;
            free(reply);

            if (!parent || parent == x11.screen->root)
                break;
            focusedWindow = parent;
        }
    }

    if (stack.activeWindow == focusedWindow)
        return;

    auto it = wmClients.find(focusedWindow);
    if (it == wmClients.end())
    {
        Activate(stack, XCB_CURRENT_TIME);
        return;
    }

    const auto &[_, client] = *it;
    if (client.transientFor)
    {
        if (client.transientFor == stack.activeWindow)
        {
            return;
        }

        if (auto it = wmClients.find(stack.activeWindow);
            it != wmClients.end() && client.transientFor == it->second.transientFor)
        {
            return;
        }
    }

    Activate(stack, XCB_CURRENT_TIME);
}

static void FetchClientProperty(xcb_window_t clientWindow, Client &client, xcb_atom_t property)
{
    if (!wmPropertyChangeHandlers.contains(property))
        return;

    auto cookie = xcb_get_property_unchecked(x11.conn, false, clientWindow, property, XCB_ATOM_ANY, 0,
                                             std::numeric_limits<uint32_t>::max());

    auto it = client.propertyCookies.find(property);
    if (it == client.propertyCookies.end())
    {
        client.propertyCookies.try_emplace(property, cookie);
    }
}

void ManageClient(xcb_window_t clientWindow)
{
    if (auto [it, inserted] = wmClients.try_emplace(clientWindow, Client{}); inserted)
    {
        xcb_change_window_attributes(
            x11.conn, clientWindow, XCB_CW_EVENT_MASK,
            (uint32_t[]){
                XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW,
            });

        for (auto &[property, _] : wmPropertyChangeHandlers)
        {
            FetchClientProperty(clientWindow, it->second, property);
        }

        wmPendingClients.emplace_back(clientWindow);
    }
}

void ManageClientsStartup()
{
    xcb_query_tree_reply_t *treeReply =
        xcb_query_tree_reply(x11.conn, xcb_query_tree(x11.conn, x11.screen->root), nullptr);
    if (!treeReply)
        return;

    std::span<xcb_window_t> children = {xcb_query_tree_children(treeReply),
                                        static_cast<size_t>(xcb_query_tree_children_length(treeReply))};

    for (xcb_window_t clientWindow : children)
    {
        xcb_get_window_attributes_reply_t *attrReply =
            xcb_get_window_attributes_reply(x11.conn, xcb_get_window_attributes(x11.conn, clientWindow), nullptr);
        if (!attrReply)
            continue;
        absl::Cleanup attrReplyFreer = [attrReply] -> void { free(attrReply); };

        if (attrReply->override_redirect)
            continue;
        if (attrReply->map_state == XCB_MAP_STATE_UNMAPPED)
            continue;

        ManageClient(clientWindow);
    }

    free(treeReply);
}

void UnmanageClient(xcb_window_t window)
{
    auto it = wmClients.find(window);
    if (it == wmClients.end())
        return;

    auto &client = it->second;

    for (auto &[_, cookie] : client.propertyCookies)
    {
        xcb_discard_reply(x11.conn, cookie.sequence);
    }

    if (client.transientFor)
    {
        CHECK(client.subwindows.empty());
        auto &subwindows = wmClients.at(client.transientFor).subwindows;
        auto it = std::ranges::find(subwindows, window);
        CHECK(it != subwindows.end());
        subwindows.erase(it);
    }
    else
    {
        for (xcb_window_t subwindow : client.subwindows)
        {
            wmClients.at(subwindow).transientFor = 0;
        }
    }

    wmClients.erase(it);

    for (size_t istack = 0; istack < wmStacks.size(); ++istack)
    {
        WindowStack &stack = wmStacks.at(istack);

        auto it = std::ranges::find(stack.windows, client.transientFor ? client.transientFor : window);
        if (it == stack.windows.end())
        {
            continue;
        }

        wmLayoutDirty = true;
        if (!client.transientFor)
        {
            wmFollow = false;
            stack.zoom = false;
            stack.windows.erase(it);
        }

        if (stack.activeWindow == window)
        {
            stack.activeWindow = 0;

            if (istack == (wmActiveStackIdx & 0xFF))
            {
                xcb_window_t fallbackTo = client.transientFor;
                if (!fallbackTo && !stack.windows.empty())
                {
                    fallbackTo = stack.windows.front();
                }

                Activate(stack, fallbackTo, XCB_CURRENT_TIME);
            }
        }

        return;
    }
}

static void ConfigureClientIfNeeded(xcb_connection_t *conn, xcb_window_t clientWindow, Client &client,
                                    const Rect &newRect, uint32_t newBorderWidth)
{
    uint16_t mask = 0;
    std::vector<uint32_t> values;
    bool anythingChanged = false;
    bool sizeChanged = false;

    if (newRect.X() != client.rect.X())
    {
        anythingChanged = true;
        mask |= XCB_CONFIG_WINDOW_X;
        values.emplace_back(newRect.X());
    }

    if (newRect.Y() != client.rect.Y())
    {
        anythingChanged = true;
        mask |= XCB_CONFIG_WINDOW_Y;
        values.emplace_back(newRect.Y());
    }

    if (newRect.Width() != client.rect.Width())
    {
        anythingChanged = true;
        sizeChanged = true;
        mask |= XCB_CONFIG_WINDOW_WIDTH;
        values.emplace_back(newRect.Width());
    }

    if (newRect.Height() != client.rect.Height())
    {
        anythingChanged = true;
        sizeChanged = true;
        mask |= XCB_CONFIG_WINDOW_HEIGHT;
        values.emplace_back(newRect.Height());
    }

    if (newBorderWidth != client.borderWidth)
    {
        anythingChanged = true;
        sizeChanged = true;
        mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        values.emplace_back(newBorderWidth);
    }

    if (anythingChanged)
    {
        xcb_configure_window(conn, clientWindow, mask, values.data());

        client.wantsConfigureNotify = !sizeChanged;
        client.rect = newRect;
        client.borderWidth = newBorderWidth;
    }
}

static void MoveStack(xcb_timestamp_t time, auto computeIdx)
{
    size_t iold = wmActiveStackIdx & 0xFF;
    size_t inew = computeIdx(iold + wmStacks.size()) % wmStacks.size();

    if (iold == inew)
        return;

    wmBackgroundDirty = true;

    WindowStack &oldstack = GetActiveStack();
    wmActiveStackIdx = inew;
    WindowStack &newstack = GetActiveStack();

    if (wmFollow)
    {
        if (oldstack.activeWindow)
        {
            newstack.activeWindow = oldstack.activeWindow;
            newstack.windows.emplace_back(oldstack.activeWindow);

            newstack.zoom = false;
            oldstack.zoom = false;

            auto it = std::ranges::find(oldstack.windows, oldstack.activeWindow);
            CHECK_NE(it, oldstack.windows.end());
            oldstack.windows.erase(it);

            if (oldstack.windows.empty())
                oldstack.activeWindow = 0;
            else
                oldstack.activeWindow = oldstack.windows.at(0);
        }
    }
    else
    {
        ApplyBorder(x11.conn, oldstack.activeWindow, Color::KNone);
        Activate(newstack, newstack.activeWindow, time);
    }

    wmLayoutDirty = true;
}

void MoveStackNext(xcb_timestamp_t time)
{
    MoveStack(time, [](auto idx) -> auto { return idx + 1; });
}

void MoveStackPrev(xcb_timestamp_t time)
{
    MoveStack(time, [](auto idx) -> auto { return idx - 1; });
}

static void MoveLocal(xcb_timestamp_t time, auto computeIdx)
{
    WindowStack &stack = GetActiveStack();
    ClearZoom(stack);

    if (stack.windows.empty())
        return;

    if (stack.activeWindow && stack.windows.size() < 2)
    {
        return;
    }

    if (stack.activeWindow)
    {
        auto it = std::ranges::find(stack.windows, stack.activeWindow);
        if (it == stack.windows.end())
            return;

        size_t iold = std::distance(stack.windows.begin(), it);
        size_t inew = computeIdx(iold + stack.windows.size()) % stack.windows.size();

        if (iold == inew)
            return;

        if (wmFollow)
        {
            std::iter_swap(stack.windows.begin() + iold, stack.windows.begin() + inew);
            wmLayoutDirty = true;
        }
        else
        {
            Activate(stack, stack.windows.at(inew), time);
        }
    }
    else
    {
        if (!wmFollow)
        {
            Activate(stack, stack.windows.front(), time);
        }
    }
}

void MoveLocalNext(xcb_timestamp_t time)
{
    MoveLocal(time, [](auto idx) -> auto { return idx + 1; });
}

void MoveLocalPrev(xcb_timestamp_t time)
{
    MoveLocal(time, [](auto idx) -> auto { return idx - 1; });
}

void NextLayout()
{
    WindowStack &stack = GetActiveStack();
    CycleLayoutType(stack.layoutType);
    wmLayoutDirty = true;
    ClearZoom(stack);
}

auto GetActiveClientBarText() -> std::string
{
    const WindowStack &stack = GetActiveStack();
    xcb_window_t activeWindow = stack.activeWindow;
    if (!activeWindow || !wmClients.contains(activeWindow))
        return "nyla: no active client";

    return wmClients.at(activeWindow).name;
}

void CloseActive()
{
    WindowStack &stack = GetActiveStack();
    if (!stack.activeWindow)
        return;

    static absl::Time last = absl::InfinitePast();
    if (absl::Now() - last >= absl::Milliseconds(100))
    {
        X11SendWmDeleteWindow(stack.activeWindow);
    }
    last = absl::Now();
}

void ToggleZoom()
{
    WindowStack &stack = GetActiveStack();
    stack.zoom ^= 1;
    wmBackgroundDirty = true;
    wmLayoutDirty = true;
    wmBorderDirty = true;
}

void ToggleFollow()
{
    WindowStack &stack = GetActiveStack();

    auto it = wmClients.find(stack.activeWindow);
    if (it == wmClients.end())
    {
        return;
    }

    Client &client = it->second;

    if (!stack.activeWindow || client.transientFor)
    {
        wmFollow = false;
        return;
    }

    wmFollow ^= 1;
    if (!wmFollow)
        ClearZoom(stack);

    wmBorderDirty = true;
}

//

void ProcessWM()
{
    for (auto &[client_window, client] : wmClients)
    {
        for (auto &[property, cookie] : client.propertyCookies)
        {
            xcb_get_property_reply_t *reply = xcb_get_property_reply(x11.conn, cookie, nullptr);
            if (!reply)
                continue;

            auto handlerIt = wmPropertyChangeHandlers.find(property);
            if (handlerIt == wmPropertyChangeHandlers.end())
            {
                LOG(ERROR) << "missing property change handler " << property;
                continue;
            }

            handlerIt->second(client_window, client, reply);
            free(reply);
        }
        client.propertyCookies.clear();
    }

    WindowStack &stack = GetActiveStack();

    if (!wmPendingClients.empty())
    {
        for (xcb_window_t clientWindow : wmPendingClients)
        {
            auto it = wmClients.find(clientWindow);
            if (it == wmClients.end())
                continue;

            auto &[_, client] = *it;
            if (client.transientFor)
            {
                bool found = false;
                for (int i = 0; i < 10; ++i)
                {
                    auto it = wmClients.find(client.transientFor);
                    if (it == wmClients.end())
                        break;

                    xcb_window_t nextTransient = it->second.transientFor;
                    if (!nextTransient)
                    {
                        found = true;
                        break;
                    }
                    client.transientFor = nextTransient;
                }
                if (!found)
                    client.transientFor = 0;
            }

            if (!client.transientFor || client.transientFor != stack.activeWindow)
                ClearZoom(stack);
        }

        bool activated = false;
        for (xcb_window_t clientWindow : wmPendingClients)
        {
            const auto &client = wmClients.at(clientWindow);
            if (client.transientFor)
            {
                Client &parent = wmClients.at(client.transientFor);
                parent.subwindows.push_back(clientWindow);
            }
            else
            {
                stack.windows.emplace_back(clientWindow);

                if (!activated)
                {
                    Activate(stack, clientWindow, XCB_CURRENT_TIME);
                    activated = true;
                }
            }
        }

        wmPendingClients.clear();
        wmFollow = false;
        wmLayoutDirty = true;
    }

    if (wmBorderDirty)
    {
        Color color = [&stack] -> nyla::Color {
            if (wmFollow)
                return Color::KActiveFollow;
            if (stack.zoom || stack.windows.size() < 2)
                return Color::KNone;
            return Color::KActive;
        }();
        ApplyBorder(x11.conn, stack.activeWindow, color);

        wmBorderDirty = false;
    }

    if (wmLayoutDirty)
    {
        Rect screenRect = Rect(x11.screen->width_in_pixels, x11.screen->height_in_pixels);
        if (!stack.zoom)
            screenRect = TryApplyMarginTop(screenRect, wmBarHeight);

        auto hide = [](xcb_window_t clientWindow, Client &client) -> void {
            ConfigureClientIfNeeded(x11.conn, clientWindow, client,
                                    Rect{x11.screen->width_in_pixels, x11.screen->height_in_pixels, client.rect.Width(),
                                         client.rect.Height()},
                                    client.borderWidth);
        };

        auto hideAll = [hide](xcb_window_t clientWindow, Client &client) -> void {
            hide(clientWindow, client);
            for (xcb_window_t subwindow : client.subwindows)
                hide(subwindow, wmClients.at(subwindow));
        };

        auto configureWindows = [](Rect boundingRect, std::span<const xcb_window_t> windows, LayoutType layoutType,
                                   auto visitor) -> auto {
            std::vector<Rect> layout = ComputeLayout(boundingRect, windows.size(), 2, layoutType);
            CHECK_EQ(layout.size(), windows.size());

            for (auto [rect, client_window] : std::ranges::views::zip(layout, windows))
            {
                Client &client = wmClients.at(client_window);

                auto center = [](uint32_t max, uint32_t &w, int32_t &x) -> void {
                    if (max)
                    {
                        uint32_t tmp = std::min(max, w);
                        x += (w - tmp) / 2;
                        w = tmp;
                    }
                };
                center(client.maxWidth, rect.Width(), rect.X());
                center(client.maxHeight, rect.Height(), rect.Y());

                ConfigureClientIfNeeded(x11.conn, client_window, client, rect, 2);

                visitor(client);
            }
        };

        auto configureSubwindows = [configureWindows](const Client &client) -> void {
            configureWindows(TryApplyMargin(client.rect, 20), client.subwindows, LayoutType::KRows,
                             [](Client &client) -> void {});
        };

        if (stack.zoom)
        {
            for (xcb_window_t clientWindow : stack.windows)
            {
                auto &client = wmClients.at(clientWindow);

                if (clientWindow != stack.activeWindow)
                {
                    hideAll(clientWindow, client);
                }
                else
                {
                    ConfigureClientIfNeeded(x11.conn, clientWindow, client, screenRect, wmFollow ? 2 : 0);

                    configureSubwindows(client);
                }
            }
        }
        else
        {
            configureWindows(screenRect, stack.windows, stack.layoutType, configureSubwindows);
        }

        for (size_t istack = 0; istack < wmStacks.size(); ++istack)
        {
            if (istack != (wmActiveStackIdx & 0xFF))
            {
                for (xcb_window_t clientWindow : wmStacks[istack].windows)
                    hideAll(clientWindow, wmClients.at(clientWindow));
            }
        }

        wmLayoutDirty = false;
    }

    for (auto &[client_window, client] : wmClients)
    {
        if (client.wantsConfigureNotify)
        {
            X11SendConfigureNotify(client_window, x11.screen->root, client.rect.X(), client.rect.Y(),
                                   client.rect.Width(), client.rect.Height(), 2);
            client.wantsConfigureNotify = false;
        }
    }
}

void ProcessWMEvents(const bool &isRunning, uint16_t modifier, std::vector<Keybind> keybinds)
{
    while (isRunning)
    {
        xcb_generic_event_t *event = xcb_poll_for_event(x11.conn);
        if (!event)
            break;
        absl::Cleanup eventFreer = [event] -> void { free(event); };

        bool isSynthethic = event->response_type & 0x80;
        uint8_t eventType = event->response_type & 0x7F;

        WindowStack &stack = GetActiveStack();

        if (isSynthethic && eventType != XCB_CLIENT_MESSAGE)
        {
            // continue;
        }

        switch (eventType)
        {
        case XCB_KEY_PRESS: {
            auto keypress = reinterpret_cast<xcb_key_press_event_t *>(event);
            if (keypress->state == modifier)
            {
                for (const auto &[keycode, mod, fn] : keybinds)
                {
                    if (mod == keypress->state && keycode == keypress->detail)
                    {
                        if (std::holds_alternative<void (*)()>(fn))
                        {
                            std::get<void (*)()>(fn)();
                        }
                        else if (std::holds_alternative<void (*)(xcb_timestamp_t time)>(fn))
                        {
                            std::get<void (*)(xcb_timestamp_t time)>(fn)(keypress->time);
                        }
                        else
                        {
                            CHECK(false);
                        }
                        break;
                    }
                }
            }
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            auto propertynotify = reinterpret_cast<xcb_property_notify_event_t *>(event);

            xcb_window_t clientWindow = propertynotify->window;
            auto it = wmClients.find(clientWindow);
            if (it != wmClients.end())
            {
                FetchClientProperty(clientWindow, it->second, propertynotify->atom);
            }
            break;
        }
        case XCB_CONFIGURE_REQUEST: {
            auto configurerequest = reinterpret_cast<xcb_configure_request_event_t *>(event);
            auto it = wmClients.find(configurerequest->window);
            if (it != wmClients.end())
            {
                it->second.wantsConfigureNotify = true;
            }
            break;
        }
        case XCB_MAP_REQUEST: {
            xcb_map_window(x11.conn, reinterpret_cast<xcb_map_request_event_t *>(event)->window);
            break;
        }
        case XCB_MAP_NOTIFY: {
            auto mapnotify = reinterpret_cast<xcb_map_notify_event_t *>(event);
            if (!mapnotify->override_redirect)
            {
                xcb_window_t window = reinterpret_cast<xcb_map_notify_event_t *>(event)->window;
                ManageClient(window);
            }
            break;
        }
        case XCB_MAPPING_NOTIFY: {
            // auto mappingnotify =
            //     reinterpret_cast<xcb_mapping_notify_event_t*>(event);
            LOG(INFO) << "mapping notify";
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
            auto focusin = reinterpret_cast<xcb_focus_in_event_t *>(event);
            if (focusin->mode == XCB_NOTIFY_MODE_NORMAL)
                CheckFocusTheft();
            break;
        }
        case XCB_EXPOSE: {
            auto expose = reinterpret_cast<xcb_expose_event_t *>(event);
            if (expose->window == backgroundWindow)
            {
                wmBackgroundDirty = true;
            }
            break;
        }

        case XCB_GE_GENERIC: {
            auto ge = reinterpret_cast<xcb_ge_generic_event_t *>(event);

            if (ge->extension == x11.extXi2->major_opcode)
            {
                switch (ge->event_type)
                {
                case XCB_INPUT_RAW_MOTION: {
                    auto rawmotion = reinterpret_cast<xcb_input_raw_motion_event_t *>(event);
                    lastRawmotionTs = std::max(lastRawmotionTs, rawmotion->time);
                    MaybeActivateUnderPointer(stack, lastRawmotionTs);
                    break;
                }
                }
            }

            break;
        }

        case XCB_ENTER_NOTIFY: {
            auto enternotify = reinterpret_cast<xcb_enter_notify_event_t *>(event);
            lastEnteredWindow = enternotify->event;
            MaybeActivateUnderPointer(stack, enternotify->time);
            break;
        }

        case 0: {
            auto error = reinterpret_cast<xcb_generic_error_t *>(event);
            LOG(ERROR) << "xcb error: " << static_cast<X11ErrorCode>(error->error_code)
                       << " sequence: " << error->sequence;
            break;
        }
        }
    }
}

void UpdateBackground()
{
    const WindowStack &stack = GetActiveStack();

    std::string activeClientName;

    if (stack.activeWindow)
    {
        auto it = wmClients.find(stack.activeWindow);
        if (it == wmClients.end())
            activeClientName = absl::StrFormat("invalid %v", stack.activeWindow);
        else
        {
            activeClientName = it->second.name;
            std::erase_if(activeClientName, [](char ch) -> bool { return ch < 0x20 || ch > 0x7F; });
        }
    }
    else
    {
        activeClientName = absl::StrFormat("nylawm %v", wmActiveStackIdx & 0xFF);
    }

    double loadAvg[3];
    getloadavg(loadAvg, std::size(loadAvg));

    std::string barText =
        absl::StrFormat("%.2f, %.2f, %.2f %s %v", loadAvg[0], loadAvg[1], loadAvg[2],
                        absl::FormatTime("%H:%M:%S %d.%m.%Y", absl::Now(), absl::LocalTimeZone()), activeClientName);

    static std::future<void> fut;
    if (IsFutureReady(fut))
    {
        wmBackgroundDirty = false;
        fut = std::async(std::launch::async, [wmClientSize = wmClients.size(), barText = std::move(barText)] -> void {
            DrawBackground(wmClientSize, barText);
        });
    }
}

static auto DumpClients() -> std::string
{
    std::string out;

    const WindowStack &stack = GetActiveStack();

    absl::StrAppendFormat(&out, "active window = %x\n\n", stack.activeWindow);

    for (const auto &[client_window, client] : wmClients)
    {
        std::string_view indent = [&client]() -> const char * {
            if (client.transientFor)
                return "  T  ";
            if (!client.subwindows.empty())
                return "  S  ";
            return "";
        }();

        std::string transientForName;
        if (client.transientFor)
        {
            auto it = wmClients.find(client.transientFor);
            if (it == wmClients.end())
            {
                transientForName = "invalid " + std::to_string(client.transientFor);
            }
            else
            {
                transientForName = it->second.name + " " + std::to_string(client.transientFor);
            }
        }
        else
        {
            transientForName = "none";
        }

        absl::StrAppendFormat(&out,
                              "%swindow=%x\n%sname=%v\n%srect=%v\n%swm_"
                              "transient_for=%v\n%sinput=%v\n"
                              "%swm_take_focus=%v\n%swm_delete_window=%v\n%"
                              "ssubwindows=%v\n%smax_dimensions=%vx%v\n\n",
                              indent, client_window, indent, client.name, indent, client.rect, indent, transientForName,
                              indent, client.wmHintsInput, indent, client.wmTakeFocus, indent, client.wmDeleteWindow,
                              indent, absl::StrJoin(client.subwindows, ", "), indent, client.maxWidth,
                              client.maxHeight);
    }
    return out;
}

} // namespace nyla