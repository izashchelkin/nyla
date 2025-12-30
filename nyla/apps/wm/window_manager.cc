#include "nyla/apps/wm/window_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

#include "nyla/apps/wm/layout.h"
#include "nyla/apps/wm/palette.h"
#include "nyla/commons/cleanup.h"
#include "nyla/commons/containers/map.h"
#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/linux/x11_error.h"
#include "nyla/platform/linux/x11_wm_hints.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"

namespace nyla
{

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

namespace
{

uint32_t wmBarHeight = 20;

bool wmLayoutDirty;
bool wmFollow;
bool wmBorderDirty;

Map<xcb_window_t, Client> wmClients;
std::vector<xcb_window_t> wmPendingClients;

Map<xcb_atom_t, void (*)(xcb_window_t, Client &, xcb_get_property_reply_t *)> wmPropertyChangeHandlers;

std::vector<WindowStack> wmStacks;
uint64_t wmActiveStackIdx;

xcb_timestamp_t lastRawmotionTs = 0;
xcb_window_t lastEnteredWindow = 0;

Platform::Impl *x11;

// TODO: this stuff can be moved into platform impl

void X11SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                            uint16_t height, uint16_t borderWidth)
{
    xcb_configure_notify_event_t event = {
        .response_type = XCB_CONFIGURE_NOTIFY,
        .window = window,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .border_width = borderWidth,
    };

    xcb_send_event(x11->GetConn(), false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
}

void X11SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                            uint32_t arg4)
{
    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = window,
        .type = type,
        .data = {.data32 = {arg1, arg2, arg3, arg4}},
    };

    xcb_send_event(x11->GetConn(), false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
}

void X11SendWmTakeFocus(xcb_window_t window, uint32_t time)
{
    X11SendClientMessage32(window, x11->GetAtoms().wm_protocols, x11->GetAtoms().wm_take_focus, time, 0, 0);
}

void X11SendWmDeleteWindow(xcb_window_t window)
{
    X11SendClientMessage32(window, x11->GetAtoms().wm_protocols, x11->GetAtoms().wm_delete_window, XCB_CURRENT_TIME, 0,
                           0);
}

//

auto GetActiveStack() -> WindowStack &
{
    NYLA_ASSERT(wmActiveStackIdx & 0xFF < wmStacks.size());
    return wmStacks.at(wmActiveStackIdx & 0xFF);
}

void HandleWmHints(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    X11WmHints wmHints = [&reply] -> X11WmHints {
        if (!reply || xcb_get_property_value_length(reply) != sizeof(X11WmHints))
            return X11WmHints{};

        return *static_cast<X11WmHints *>(xcb_get_property_value(reply));
    }();

    Initialize(wmHints);

    client.wmHintsInput = wmHints.input;

    // if (wm_hints.urgent() && !client.urgent) indicator?
    client.urgent = wmHints.Urgent();
}

void HandleWmNormalHints(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    X11WmNormalHints wmNormalHints = [&reply] -> X11WmNormalHints {
        if (!reply || xcb_get_property_value_length(reply) != sizeof(X11WmNormalHints))
            return X11WmNormalHints{};

        return *static_cast<X11WmNormalHints *>(xcb_get_property_value(reply));
    }();

    Initialize(wmNormalHints);

    client.maxWidth = wmNormalHints.maxWidth;
    client.maxHeight = wmNormalHints.maxHeight;
}

void HandleWmName(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    if (!reply)
    {
        NYLA_LOG("property fetch error");
        return;
    }

    client.name = {static_cast<char *>(xcb_get_property_value(reply)),
                   static_cast<size_t>(xcb_get_property_value_length(reply))};
}

void HandleWmProtocols(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
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

    for (xcb_atom_t atom : wmProtocols)
    {
        if (atom == x11->GetAtoms().wm_delete_window)
        {
            client.wmDeleteWindow = true;
            continue;
        }
        if (atom == x11->GetAtoms().wm_take_focus)
        {
            client.wmTakeFocus = true;
            continue;
        }
    }
}

void HandleWmTransientFor(xcb_window_t clientWindow, Client &client, xcb_get_property_reply_t *reply)
{
    if (!reply || !reply->length)
        return;
    if (reply->type != XCB_ATOM_WINDOW)
        return;

    if (client.transientFor != 0)
        return;

    client.transientFor = *reinterpret_cast<xcb_window_t *>(xcb_get_property_value(reply));
}

void ClearZoom(WindowStack &stack)
{
    if (!stack.zoom)
        return;

    stack.zoom = false;
    wmLayoutDirty = true;
}

void ApplyBorder(xcb_connection_t *conn, xcb_window_t window, Color color)
{
    if (!window)
        return;
    xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, &color);
}

void Activate(const WindowStack &stack, xcb_timestamp_t time)
{
    if (!stack.activeWindow)
    {
        goto revert_to_root;
    }

    if (auto it = wmClients.find(stack.activeWindow); it != wmClients.end())
    {
        wmBorderDirty = true;

        const auto &client = it->second;

        xcb_window_t immediateFocus = client.wmHintsInput ? stack.activeWindow : x11->GetRoot();

        xcb_set_input_focus(x11->GetConn(), XCB_INPUT_FOCUS_NONE, immediateFocus, time);

        if (client.wmTakeFocus)
        {
            X11SendWmTakeFocus(stack.activeWindow, time);
        }

        return;
    }

revert_to_root:
    xcb_set_input_focus(x11->GetConn(), XCB_INPUT_FOCUS_NONE, x11->GetRoot(), time);
    lastEnteredWindow = 0;
}

void Activate(WindowStack &stack, xcb_window_t clientWindow, xcb_timestamp_t time)
{
    if (stack.activeWindow != clientWindow)
    {
        ApplyBorder(x11->GetConn(), stack.activeWindow, Color::KNone);
        stack.activeWindow = clientWindow;
        // wmBackgroundDirty = true;
    }

    Activate(stack, time);
}

void MaybeActivateUnderPointer(WindowStack &stack, xcb_timestamp_t ts)
{
    if (stack.zoom)
        return;
    if (wmFollow)
        return;

    if (!lastEnteredWindow)
    {
        return;
    }
    if (lastEnteredWindow == x11->GetRoot())
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

void CheckFocusTheft()
{
    auto reply = xcb_get_input_focus_reply(x11->GetConn(), xcb_get_input_focus(x11->GetConn()), nullptr);
    xcb_window_t focusedWindow = reply->focus;
    free(reply);

    const WindowStack &stack = GetActiveStack();
    if (stack.activeWindow == focusedWindow)
        return;

    if (focusedWindow == x11->GetRoot())
        return;
    if (!focusedWindow)
        return;

    if (!wmClients.contains(focusedWindow))
    {
        for (;;)
        {
            xcb_query_tree_reply_t *reply =
                xcb_query_tree_reply(x11->GetConn(), xcb_query_tree(x11->GetConn(), focusedWindow), nullptr);

            if (!reply)
            {
                Activate(stack, XCB_CURRENT_TIME);
                return;
            }

            xcb_window_t parent = reply->parent;
            free(reply);

            if (!parent || parent == x11->GetRoot())
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

void FetchClientProperty(xcb_window_t clientWindow, Client &client, xcb_atom_t property)
{
    if (!wmPropertyChangeHandlers.contains(property))
        return;

    auto cookie = xcb_get_property_unchecked(x11->GetConn(), false, clientWindow, property, XCB_ATOM_ANY, 0,
                                             std::numeric_limits<uint32_t>::max());

    auto it = client.propertyCookies.find(property);
    if (it == client.propertyCookies.end())
    {
        client.propertyCookies.try_emplace(property, cookie);
    }
}

} // namespace

void InitializeWM()
{
    x11 = g_Platform->GetImpl();

    wmStacks.resize(9);

    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_HINTS, HandleWmHints);
    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_NORMAL_HINTS, HandleWmNormalHints);
    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_NAME, HandleWmName);
    wmPropertyChangeHandlers.try_emplace(x11->GetAtoms().wm_protocols, HandleWmProtocols);
    wmPropertyChangeHandlers.try_emplace(XCB_ATOM_WM_TRANSIENT_FOR, HandleWmTransientFor);
}

void ManageClient(xcb_window_t clientWindow)
{
    if (auto [it, inserted] = wmClients.try_emplace(clientWindow, Client{}); inserted)
    {
        xcb_change_window_attributes(
            x11->GetConn(), clientWindow, XCB_CW_EVENT_MASK,
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
        xcb_query_tree_reply(x11->GetConn(), xcb_query_tree(x11->GetConn(), x11->GetRoot()), nullptr);
    if (!treeReply)
        return;

    std::span<xcb_window_t> children = {xcb_query_tree_children(treeReply),
                                        static_cast<size_t>(xcb_query_tree_children_length(treeReply))};

    for (xcb_window_t clientWindow : children)
    {
        xcb_get_window_attributes_reply_t *attrReply = xcb_get_window_attributes_reply(
            x11->GetConn(), xcb_get_window_attributes(x11->GetConn(), clientWindow), nullptr);
        if (!attrReply)
            continue;
        Cleanup attrReplyFreer([attrReply] -> void { free(attrReply); });

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
        xcb_discard_reply(x11->GetConn(), cookie.sequence);
    }

    if (client.transientFor)
    {
        NYLA_ASSERT(client.subwindows.empty());
        auto &subwindows = wmClients.at(client.transientFor).subwindows;
        auto it = std::ranges::find(subwindows, window);
        NYLA_ASSERT(it != subwindows.end());
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

    // wmBackgroundDirty = true;

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
            NYLA_ASSERT(it != oldstack.windows.end());
            oldstack.windows.erase(it);

            if (oldstack.windows.empty())
                oldstack.activeWindow = 0;
            else
                oldstack.activeWindow = oldstack.windows.at(0);
        }
    }
    else
    {
        ApplyBorder(x11->GetConn(), oldstack.activeWindow, Color::KNone);
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

    static uint64_t last = 0;
    uint64_t now = GetMonotonicTimeMillis();
    if (now - last >= 100)
    {
        X11SendWmDeleteWindow(stack.activeWindow);
    }
    last = now;
}

void ToggleZoom()
{
    WindowStack &stack = GetActiveStack();
    stack.zoom ^= 1;
    // wmBackgroundDirty = true;
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
            xcb_get_property_reply_t *reply = xcb_get_property_reply(x11->GetConn(), cookie, nullptr);
            if (!reply)
                continue;

            auto handlerIt = wmPropertyChangeHandlers.find(property);
            if (handlerIt == wmPropertyChangeHandlers.end())
            {
                NYLA_LOG("missing property change handler %d", property);
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
        ApplyBorder(x11->GetConn(), stack.activeWindow, color);

        wmBorderDirty = false;
    }

    if (wmLayoutDirty)
    {
        Rect screenRect = Rect(x11->GetScreen()->width_in_pixels, x11->GetScreen()->height_in_pixels);
        if (!stack.zoom)
            screenRect = TryApplyMarginTop(screenRect, wmBarHeight);

        auto hide = [](xcb_window_t clientWindow, Client &client) -> void {
            ConfigureClientIfNeeded(x11->GetConn(), clientWindow, client,
                                    Rect{x11->GetScreen()->width_in_pixels, x11->GetScreen()->height_in_pixels,
                                         client.rect.Width(), client.rect.Height()},
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
            NYLA_ASSERT(layout.size() == windows.size());

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

                ConfigureClientIfNeeded(x11->GetConn(), client_window, client, rect, 2);

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
                    ConfigureClientIfNeeded(x11->GetConn(), clientWindow, client, screenRect, wmFollow ? 2 : 0);

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
            X11SendConfigureNotify(client_window, x11->GetRoot(), client.rect.X(), client.rect.Y(), client.rect.Width(),
                                   client.rect.Height(), 2);
            client.wantsConfigureNotify = false;
        }
    }
}

void ProcessWMEvents(const bool &isRunning, uint16_t modifier, std::vector<Keybind> keybinds)
{
    while (isRunning)
    {
        xcb_generic_event_t *event = xcb_poll_for_event(x11->GetConn());
        if (!event)
            break;
        Cleanup eventFreer([event] -> void { free(event); });

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
                            NYLA_ASSERT(false);
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
            xcb_map_window(x11->GetConn(), reinterpret_cast<xcb_map_request_event_t *>(event)->window);
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
            NYLA_LOG("mapping notify");
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

        case XCB_GE_GENERIC: {
            auto ge = reinterpret_cast<xcb_ge_generic_event_t *>(event);

            if (ge->extension == x11->GetXInputExtensionMajorOpCode())
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
            NYLA_LOG("xcb error: %d, sequence: ", error->error_code, error->sequence);
            break;
        }
        }
    }
}

} // namespace nyla