#include "nyla/apps/wm/window_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include "nyla/apps/wm/layout.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/cleanup.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/log.h"
#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/linux/x11_wm_hints.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"

namespace nyla
{

void WindowManager::Init()
{
    m_X11 = g_Platform->GetImpl();

    m_Stacks.resize(9);

    if (xcb_request_check(m_X11->GetConn(),
                          xcb_change_window_attributes_checked(
                              m_X11->GetConn(), m_X11->GetRoot(), XCB_CW_EVENT_MASK,
                              (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                           XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                           XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                           XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_POINTER_MOTION})))
    {
        NYLA_ASSERT(false && "another wm is already running");
    }

    m_X11->Grab();

    xcb_query_tree_reply_t *treeReply =
        xcb_query_tree_reply(m_X11->GetConn(), xcb_query_tree(m_X11->GetConn(), m_X11->GetRoot()), nullptr);
    if (!treeReply)
        return;

    std::span<xcb_window_t> children = {xcb_query_tree_children(treeReply),
                                        static_cast<size_t>(xcb_query_tree_children_length(treeReply))};

    for (xcb_window_t clientWindow : children)
    {
        xcb_get_window_attributes_reply_t *attrReply = xcb_get_window_attributes_reply(
            m_X11->GetConn(), xcb_get_window_attributes(m_X11->GetConn(), clientWindow), nullptr);
        if (!attrReply)
            continue;
        Cleanup attrReplyFreer([=] -> void { free(attrReply); });

        if (attrReply->override_redirect)
            continue;
        if (attrReply->map_state == XCB_MAP_STATE_UNMAPPED)
            continue;

        ManageClient(clientWindow);
    }

    free(treeReply);

    auto grabKey = [this](uint32_t meta, uint32_t alt, uint32_t control, uint32_t shift, KeyPhysical key) -> void {
        uint32_t mod = 0;
        if (meta)
            mod |= XCB_MOD_MASK_4;
        if (alt)
            mod |= XCB_MOD_MASK_1;
        if (control)
            mod |= XCB_MOD_MASK_CONTROL;
        if (shift)
            mod |= XCB_MOD_MASK_SHIFT;

        uint32_t keycode = m_X11->KeyPhysicalToKeyCode(key);
        const xcb_generic_error_t *error = xcb_request_check(
            m_X11->GetConn(), xcb_grab_key_checked(m_X11->GetConn(), 1, m_X11->GetRoot(), mod, keycode,
                                                   XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC));
        NYLA_ASSERT(!error);
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

    m_X11->Flush();
    m_X11->Ungrab();
}

auto WindowManager::GetActiveStack() -> WindowStack &
{
    NYLA_ASSERT((m_ActiveStackIdx & 0xFF) < m_Stacks.size());
    return m_Stacks.at(m_ActiveStackIdx & 0xFF);
}

void WindowManager::FetchClientProperty(xcb_window_t clientWindow, Client &client, xcb_atom_t property)
{
    switch (property)
    {
    case XCB_ATOM_WM_HINTS:
    case XCB_ATOM_WM_NORMAL_HINTS:
    case XCB_ATOM_WM_NAME:
    case XCB_ATOM_WM_TRANSIENT_FOR:
        break;

    default: {
        if (property == m_X11->GetAtoms().wm_protocols)
            break;

        return;
    }
    }

    auto cookie = xcb_get_property_unchecked(m_X11->GetConn(), false, clientWindow, property, XCB_ATOM_ANY, 0,
                                             std::numeric_limits<uint32_t>::max());
    client.propertyCookies.try_emplace(property, cookie);
}

void WindowManager::ManageClient(xcb_window_t clientWindow)
{
    auto [it, inserted] = m_Clients.try_emplace(clientWindow, Client{});
    if (!inserted)
        return;

    const uint32_t eventMask =
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW;
    xcb_change_window_attributes(m_X11->GetConn(), clientWindow, XCB_CW_EVENT_MASK, &eventMask);

    Client &client = it->second;

    FetchClientProperty(clientWindow, client, XCB_ATOM_WM_HINTS);
    FetchClientProperty(clientWindow, client, XCB_ATOM_WM_NORMAL_HINTS);
    FetchClientProperty(clientWindow, client, XCB_ATOM_WM_NAME);
    FetchClientProperty(clientWindow, client, XCB_ATOM_WM_TRANSIENT_FOR);
    FetchClientProperty(clientWindow, client, m_X11->GetAtoms().wm_protocols);

    m_PendingClients.emplace_back(clientWindow);
}

void WindowManager::UnmanageClient(xcb_window_t clientWindow)
{
    auto it = m_Clients.find(clientWindow);
    if (it == m_Clients.end())
        return;

    auto &client = it->second;

    for (auto &[_, cookie] : client.propertyCookies)
    {
        xcb_discard_reply(m_X11->GetConn(), cookie.sequence);
    }

    if (client.transientFor)
    {
        NYLA_ASSERT(client.subwindows.empty());
        auto &subwindows = m_Clients.at(client.transientFor).subwindows;
        auto it = std::ranges::find(subwindows, clientWindow);
        NYLA_ASSERT(it != subwindows.end());
        subwindows.erase(it);
    }
    else
    {
        for (xcb_window_t subwindow : client.subwindows)
        {
            m_Clients.at(subwindow).transientFor = 0;
        }
    }

    m_Clients.erase(it);

    for (size_t istack = 0; istack < m_Stacks.size(); ++istack)
    {
        WindowStack &stack = m_Stacks.at(istack);

        auto it = std::ranges::find(stack.windows, client.transientFor ? client.transientFor : clientWindow);
        if (it == stack.windows.end())
        {
            continue;
        }

        m_LayoutDirty = true;
        if (!client.transientFor)
        {
            m_Follow = false;
            stack.zoom = false;
            stack.windows.erase(it);
        }

        if (stack.activeWindow == clientWindow)
        {
            stack.activeWindow = 0;

            if (istack == (m_ActiveStackIdx & 0xFF))
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

void WindowManager::Activate(const WindowStack &stack, xcb_timestamp_t time)
{
    auto revertToRoot = [&] {
        xcb_set_input_focus(m_X11->GetConn(), XCB_INPUT_FOCUS_NONE, m_X11->GetRoot(), time);
        m_LastEnteredWindow = 0;
    };

    if (!stack.activeWindow)
    {
        revertToRoot();
        return;
    }

    auto it = m_Clients.find(stack.activeWindow);
    if (it == m_Clients.end())
    {
        revertToRoot();
        return;
    }

    m_BorderDirty = true;

    const auto &client = it->second;
    xcb_window_t immediateFocus;
    if (client.wmHintsInput)
        immediateFocus = stack.activeWindow;
    else
        immediateFocus = m_X11->GetRoot();

    xcb_set_input_focus(m_X11->GetConn(), XCB_INPUT_FOCUS_NONE, immediateFocus, time);

    if (client.wmTakeFocus)
        m_X11->SendWmTakeFocus(stack.activeWindow, time);
}

void WindowManager::Activate(WindowStack &stack, xcb_window_t clientWindow, xcb_timestamp_t time)
{
    if (stack.activeWindow != clientWindow)
    {
        ApplyBorder(m_X11->GetConn(), stack.activeWindow, Color::KNone);
        stack.activeWindow = clientWindow;
        // wmBackgroundDirty = true;
    }

    Activate(stack, time);
}

void WindowManager::ApplyBorder(xcb_connection_t *conn, xcb_window_t window, Color color)
{
    if (!window)
        return;
    xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, &color);
}

void WindowManager::CheckFocusTheft()
{
    auto reply = xcb_get_input_focus_reply(m_X11->GetConn(), xcb_get_input_focus(m_X11->GetConn()), nullptr);
    xcb_window_t focusedWindow = reply->focus;
    free(reply);

    const WindowStack &stack = GetActiveStack();
    if (stack.activeWindow == focusedWindow)
        return;

    if (focusedWindow == m_X11->GetRoot())
        return;
    if (!focusedWindow)
        return;

    if (!m_Clients.contains(focusedWindow))
    {
        for (;;)
        {
            xcb_query_tree_reply_t *reply =
                xcb_query_tree_reply(m_X11->GetConn(), xcb_query_tree(m_X11->GetConn(), focusedWindow), nullptr);

            if (!reply)
            {
                Activate(stack, XCB_CURRENT_TIME);
                return;
            }

            xcb_window_t parent = reply->parent;
            free(reply);

            if (!parent || parent == m_X11->GetRoot())
                break;
            focusedWindow = parent;
        }
    }

    if (stack.activeWindow == focusedWindow)
        return;

    auto it = m_Clients.find(focusedWindow);
    if (it == m_Clients.end())
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

        if (auto it = m_Clients.find(stack.activeWindow);
            it != m_Clients.end() && client.transientFor == it->second.transientFor)
        {
            return;
        }
    }

    Activate(stack, XCB_CURRENT_TIME);
}

void WindowManager::MaybeActivateUnderPointer(WindowStack &stack, xcb_timestamp_t ts)
{
    if (stack.zoom)
        return;
    if (m_Follow)
        return;

    if (!m_LastEnteredWindow)
    {
        return;
    }
    if (m_LastEnteredWindow == m_X11->GetRoot())
    {
        return;
    }
    if (m_LastEnteredWindow == stack.activeWindow)
    {
        return;
    }

    if (m_LastRawmotionTs > ts)
        return;
    if (ts - m_LastRawmotionTs > 3)
        return;

    if (m_Clients.find(m_LastEnteredWindow) != m_Clients.end())
    {
        Activate(stack, m_LastEnteredWindow, ts);
    }
}

void WindowManager::ClearZoom(WindowStack &stack)
{
    if (!stack.zoom)
        return;

    stack.zoom = false;
    m_LayoutDirty = true;
}

void WindowManager::Process(bool &isRunning)
{
    while (isRunning)
    {
        xcb_generic_event_t *event = xcb_poll_for_event(m_X11->GetConn());
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

            const bool meta = keypress->state & XCB_MOD_MASK_4;
            const bool control = keypress->state & XCB_MOD_MASK_CONTROL;
            const bool alt = keypress->state & XCB_MOD_MASK_1;
            const bool shift = keypress->state & XCB_MOD_MASK_SHIFT;

            KeyPhysical key;
            if (!m_X11->KeyCodeToKeyPhysical(keypress->detail, &key))
                break;

            if (meta && control && (key == KeyPhysical::ArrowLeft))
            {
                MoveStackPrev(keypress->time);
                break;
            }

            if (meta && control && (key == KeyPhysical::ArrowRight))
            {
                MoveStackNext(keypress->time);
                break;
            }

            if (meta && (key == KeyPhysical::E))
            {
                MoveStackPrev(keypress->time);
                break;
            }

            if (meta && (key == KeyPhysical::R))
            {
                MoveStackNext(keypress->time);
                break;
            }

            if (alt && shift && (key == KeyPhysical::Tab))
            {
                MoveLocalPrev(keypress->time, false);
                break;
            }

            if (alt && (key == KeyPhysical::Tab))
            {
                MoveLocalNext(keypress->time, false);
                break;
            }

            if (meta && (key == KeyPhysical::D))
            {
                MoveLocalPrev(keypress->time, true);
                break;
            }

            if (meta && (key == KeyPhysical::F))
            {
                MoveLocalNext(keypress->time, true);
                break;
            }

            if (meta && key == KeyPhysical::G)
            {
                ToggleZoom();
                break;
            }

            if (meta && (key == KeyPhysical::V))
            {
                ToggleFollow();
                break;
            }

            if (meta && (key == KeyPhysical::T))
            {
                Spawn({{"ghostty", nullptr}});
                break;
            }

            if (meta && (key == KeyPhysical::S))
            {
                Spawn({{"dmenu_run", nullptr}});
                break;
            }

            if (meta && (key == KeyPhysical::W))
            {
                NextLayout();
                break;
            }

            if (alt && (key == KeyPhysical::F4))
            {
                CloseActive();
                break;
            }

            break;
        }

        case XCB_PROPERTY_NOTIFY: {
            auto propertynotify = reinterpret_cast<xcb_property_notify_event_t *>(event);

            xcb_window_t clientWindow = propertynotify->window;
            auto it = m_Clients.find(clientWindow);
            if (it != m_Clients.end())
            {
                FetchClientProperty(clientWindow, it->second, propertynotify->atom);
            }
            break;
        }

        case XCB_CONFIGURE_REQUEST: {
            auto configurerequest = reinterpret_cast<xcb_configure_request_event_t *>(event);
            auto it = m_Clients.find(configurerequest->window);
            if (it != m_Clients.end())
            {
                it->second.wantsConfigureNotify = true;
            }
            break;
        }

        case XCB_MAP_REQUEST: {
            xcb_map_window(m_X11->GetConn(), reinterpret_cast<xcb_map_request_event_t *>(event)->window);
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

            if (ge->extension == m_X11->GetXInputExtensionMajorOpCode())
            {
                switch (ge->event_type)
                {
                case XCB_INPUT_RAW_MOTION: {
                    auto rawmotion = reinterpret_cast<xcb_input_raw_motion_event_t *>(event);
                    m_LastRawmotionTs = std::max(m_LastRawmotionTs, rawmotion->time);
                    MaybeActivateUnderPointer(stack, m_LastRawmotionTs);
                    break;
                }
                }
            }

            break;
        }

        case XCB_ENTER_NOTIFY: {
            auto enternotify = reinterpret_cast<xcb_enter_notify_event_t *>(event);
            m_LastEnteredWindow = enternotify->event;
            MaybeActivateUnderPointer(stack, enternotify->time);
            break;
        }

        case 0: {
            auto error = reinterpret_cast<xcb_generic_error_t *>(event);
            NYLA_LOG("xcb error: %d, sequence: %d", error->error_code, error->sequence);
            break;
        }
        }
    }

    if (!isRunning)
        return;

    for (auto &[client_window, client] : m_Clients)
    {
        for (auto &[property, cookie] : client.propertyCookies)
        {
            xcb_get_property_reply_t *reply = xcb_get_property_reply(m_X11->GetConn(), cookie, nullptr);
            if (!reply)
                continue;

            switch (property)
            {

            case XCB_ATOM_WM_HINTS: {

                X11WmHints wmHints = [&reply] -> X11WmHints {
                    if (!reply || xcb_get_property_value_length(reply) != sizeof(X11WmHints))
                        return X11WmHints{};

                    return *static_cast<X11WmHints *>(xcb_get_property_value(reply));
                }();

                Initialize(wmHints);

                client.wmHintsInput = wmHints.input;

                // if (wm_hints.urgent() && !client.urgent) indicator?
                client.urgent = wmHints.Urgent();

                break;
            }

            case XCB_ATOM_WM_NORMAL_HINTS: {
                X11WmNormalHints wmNormalHints = [&reply] -> X11WmNormalHints {
                    if (!reply || xcb_get_property_value_length(reply) != sizeof(X11WmNormalHints))
                        return X11WmNormalHints{};

                    return *static_cast<X11WmNormalHints *>(xcb_get_property_value(reply));
                }();

                Initialize(wmNormalHints);

                client.maxWidth = wmNormalHints.maxWidth;
                client.maxHeight = wmNormalHints.maxHeight;

                break;
            }

            case XCB_ATOM_WM_NAME: {
                if (!reply)
                {
                    NYLA_LOG("property fetch error");
                    return;
                }

                client.name = {static_cast<char *>(xcb_get_property_value(reply)),
                               static_cast<size_t>(xcb_get_property_value_length(reply))};

                break;
            }

            case XCB_ATOM_WM_TRANSIENT_FOR: {
                if (!reply || !reply->length)
                    return;
                if (reply->type != XCB_ATOM_WINDOW)
                    return;

                if (client.transientFor != 0)
                    return;

                client.transientFor = *reinterpret_cast<xcb_window_t *>(xcb_get_property_value(reply));
                break;
            }

            default: {
                if (property == m_X11->GetAtoms().wm_protocols)
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
                        if (atom == m_X11->GetAtoms().wm_delete_window)
                        {
                            client.wmDeleteWindow = true;
                            continue;
                        }
                        if (atom == m_X11->GetAtoms().wm_take_focus)
                        {
                            client.wmTakeFocus = true;
                            continue;
                        }
                    }

                    break;
                }
            }
            }

            free(reply);
        }
        client.propertyCookies.clear();
    }

    WindowStack &stack = GetActiveStack();

    if (!m_PendingClients.empty())
    {
        for (xcb_window_t clientWindow : m_PendingClients)
        {
            auto it = m_Clients.find(clientWindow);
            if (it == m_Clients.end())
                continue;

            auto &[_, client] = *it;
            if (client.transientFor)
            {
                bool found = false;
                for (int i = 0; i < 10; ++i)
                {
                    auto it = m_Clients.find(client.transientFor);
                    if (it == m_Clients.end())
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
        for (xcb_window_t clientWindow : m_PendingClients)
        {
            const auto &client = m_Clients.at(clientWindow);
            if (client.transientFor)
            {
                Client &parent = m_Clients.at(client.transientFor);
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

        m_PendingClients.clear();
        m_Follow = false;
        m_LayoutDirty = true;
    }

    if (m_BorderDirty)
    {
        Color color = [this, &stack] -> nyla::Color {
            if (m_Follow)
                return Color::KActiveFollow;
            if (stack.zoom || stack.windows.size() < 2)
                return Color::KNone;
            return Color::KActive;
        }();
        ApplyBorder(m_X11->GetConn(), stack.activeWindow, color);

        m_BorderDirty = false;
    }

    if (m_LayoutDirty)
    {
        Rect screenRect = Rect(m_X11->GetScreen()->width_in_pixels, m_X11->GetScreen()->height_in_pixels);
        if (!stack.zoom)
            screenRect = TryApplyMarginTop(screenRect, m_BarHeight);

        auto hide = [this](xcb_window_t clientWindow, Client &client) -> void {
            ConfigureClientIfNeeded(m_X11->GetConn(), clientWindow, client,
                                    Rect{m_X11->GetScreen()->width_in_pixels, m_X11->GetScreen()->height_in_pixels,
                                         client.rect.Width(), client.rect.Height()},
                                    client.borderWidth);
        };

        auto hideAll = [this, hide](xcb_window_t clientWindow, Client &client) -> void {
            hide(clientWindow, client);
            for (xcb_window_t subwindow : client.subwindows)
                hide(subwindow, m_Clients.at(subwindow));
        };

        auto configureWindows = [this](Rect boundingRect, std::span<const xcb_window_t> windows, LayoutType layoutType,
                                       auto visitor) -> auto {
            std::vector<Rect> layout = ComputeLayout(boundingRect, windows.size(), 2, layoutType);
            NYLA_ASSERT(layout.size() == windows.size());

            for (auto [rect, client_window] : std::ranges::views::zip(layout, windows))
            {
                Client &client = m_Clients.at(client_window);

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

                ConfigureClientIfNeeded(m_X11->GetConn(), client_window, client, rect, 2);

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
                auto &client = m_Clients.at(clientWindow);

                if (clientWindow != stack.activeWindow)
                {
                    hideAll(clientWindow, client);
                }
                else
                {
                    ConfigureClientIfNeeded(m_X11->GetConn(), clientWindow, client, screenRect, m_Follow ? 2 : 0);

                    configureSubwindows(client);
                }
            }
        }
        else
        {
            configureWindows(screenRect, stack.windows, stack.layoutType, configureSubwindows);
        }

        for (size_t istack = 0; istack < m_Stacks.size(); ++istack)
        {
            if (istack != (m_ActiveStackIdx & 0xFF))
            {
                for (xcb_window_t clientWindow : m_Stacks[istack].windows)
                    hideAll(clientWindow, m_Clients.at(clientWindow));
            }
        }

        m_LayoutDirty = false;
    }

    for (auto &[client_window, client] : m_Clients)
    {
        if (client.wantsConfigureNotify)
        {
            m_X11->SendConfigureNotify(client_window, m_X11->GetRoot(), client.rect.X(), client.rect.Y(),
                                       client.rect.Width(), client.rect.Height(), 2);
            client.wantsConfigureNotify = false;
        }
    }
}

void WindowManager::ConfigureClientIfNeeded(xcb_connection_t *conn, xcb_window_t clientWindow, Client &client,
                                            const Rect &newRect, uint32_t newBorderWidth)
{
    uint16_t mask = 0;
    InlineVec<uint32_t, 5> values;
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

void WindowManager::MoveStack(xcb_timestamp_t time, auto computeIdx)
{
    size_t iold = m_ActiveStackIdx & 0xFF;
    size_t inew = computeIdx(iold + m_Stacks.size()) % m_Stacks.size();

    if (iold == inew)
        return;

    // wmBackgroundDirty = true;

    WindowStack &oldstack = GetActiveStack();
    m_ActiveStackIdx = inew;
    WindowStack &newstack = GetActiveStack();

    if (m_Follow)
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
        ApplyBorder(m_X11->GetConn(), oldstack.activeWindow, Color::KNone);
        Activate(newstack, newstack.activeWindow, time);
    }

    m_LayoutDirty = true;
}

void WindowManager::MoveStackNext(xcb_timestamp_t time)
{
    MoveStack(time, [](auto idx) -> auto { return idx + 1; });
}

void WindowManager::MoveStackPrev(xcb_timestamp_t time)
{
    MoveStack(time, [](auto idx) -> auto { return idx - 1; });
}

void WindowManager::MoveLocal(xcb_timestamp_t time, auto computeIdx, bool clearZoom)
{
    WindowStack &stack = GetActiveStack();
    if (clearZoom)
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

        if (m_Follow)
        {
            std::iter_swap(stack.windows.begin() + iold, stack.windows.begin() + inew);
            m_LayoutDirty = true;
        }
        else
        {
            Activate(stack, stack.windows.at(inew), time);

            if (!clearZoom)
                m_LayoutDirty = true;
        }
    }
    else
    {
        if (!m_Follow)
        {
            Activate(stack, stack.windows.front(), time);
        }
    }
}

void WindowManager::MoveLocalNext(xcb_timestamp_t time, bool clearZoom)
{
    MoveLocal(time, [](auto idx) -> auto { return idx + 1; }, clearZoom);
}

void WindowManager::MoveLocalPrev(xcb_timestamp_t time, bool clearZoom)
{
    MoveLocal(time, [](auto idx) -> auto { return idx - 1; }, clearZoom);
}

void WindowManager::NextLayout()
{
    WindowStack &stack = GetActiveStack();
    CycleLayoutType(stack.layoutType);
    m_LayoutDirty = true;
    ClearZoom(stack);
}

void WindowManager::CloseActive()
{
    WindowStack &stack = GetActiveStack();
    if (!stack.activeWindow)
        return;

    static uint64_t last = 0;
    uint64_t now = GetMonotonicTimeMillis();
    if (now - last >= 100)
    {
        m_X11->SendWmDeleteWindow(stack.activeWindow);
    }
    last = now;
}

void WindowManager::ToggleZoom()
{
    WindowStack &stack = GetActiveStack();
    stack.zoom ^= 1;
    // wmBackgroundDirty = true;
    m_LayoutDirty = true;
    m_BorderDirty = true;
}

void WindowManager::ToggleFollow()
{
    WindowStack &stack = GetActiveStack();

    auto it = m_Clients.find(stack.activeWindow);
    if (it == m_Clients.end())
    {
        return;
    }

    Client &client = it->second;

    if (!stack.activeWindow || client.transientFor)
    {
        m_Follow = false;
        return;
    }

    m_Follow ^= 1;
    if (!m_Follow)
        ClearZoom(stack);

    m_BorderDirty = true;
}

} // namespace nyla