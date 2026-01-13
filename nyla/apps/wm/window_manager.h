#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "nyla/apps/wm/layout.h"
#include "nyla/platform/linux/platform_linux.h"
#include "xcb/xproto.h"

namespace nyla
{

enum class Color : uint32_t
{
    KNone = 0x000000,
    KActive = 0x95A3B3,
    KReserved0 = 0xC0FF1F,
    KActiveFollow = 0x84DCC6,
    KReserved1 = 0x5E1FFF,
};

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

    std::unordered_map<xcb_atom_t, xcb_get_property_cookie_t> propertyCookies;
};

class WindowManager
{
  public:
    void Init();

    void Process(bool &outIsRunning);

    void MoveLocalNext(xcb_timestamp_t time, bool clearZoom);
    void MoveLocalPrev(xcb_timestamp_t time, bool clearZoom);

    void MoveStackNext(xcb_timestamp_t time);
    void MoveStackPrev(xcb_timestamp_t time);

    void ToggleZoom();
    void ToggleFollow();

    void NextLayout();

    void CloseActive();

  private:
    auto GetActiveStack() -> WindowStack &;
    void FetchClientProperty(xcb_window_t clientWindow, Client &client, xcb_atom_t property);
    void ManageClient(xcb_window_t clientWindow);
    void UnmanageClient(xcb_window_t clientWindow);
    void Activate(const WindowStack &stack, xcb_timestamp_t time);
    void Activate(WindowStack &stack, xcb_window_t clientWindow, xcb_timestamp_t time);
    void ApplyBorder(xcb_connection_t *conn, xcb_window_t window, Color color);
    void CheckFocusTheft();
    void MaybeActivateUnderPointer(WindowStack &stack, xcb_timestamp_t ts);
    void ClearZoom(WindowStack &stack);
    void ConfigureClientIfNeeded(xcb_connection_t *conn, xcb_window_t clientWindow, Client &client, const Rect &newRect,
                                 uint32_t newBorderWidth);
    void MoveStack(xcb_timestamp_t time, auto computeIdx);
    void MoveLocal(xcb_timestamp_t time, auto computeIdx, bool clearZoom);

    uint32_t m_BarHeight = 20;

    bool m_LayoutDirty;
    bool m_Follow;
    bool m_BorderDirty;

    std::unordered_map<xcb_window_t, Client> m_Clients;
    std::vector<xcb_window_t> m_PendingClients;

    std::vector<WindowStack> m_Stacks;
    uint64_t m_ActiveStackIdx;

    xcb_timestamp_t m_LastRawmotionTs = 0;
    xcb_window_t m_LastEnteredWindow = 0;

    Platform::Impl *m_X11;
};

} // namespace nyla