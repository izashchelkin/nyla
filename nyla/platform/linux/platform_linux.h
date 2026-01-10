#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"
#include <xkbcommon/xkbcommon-x11.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <xcb/xkb.h>

#undef explicit

namespace nyla
{

// NOLINTBEGIN
#define NYLA_X11_ATOMS(X)                                                                                              \
    X(compound_text)                                                                                                   \
    X(wm_delete_window)                                                                                                \
    X(wm_protocols)                                                                                                    \
    X(wm_name)                                                                                                         \
    X(wm_state)                                                                                                        \
    X(wm_take_focus)
// NOLINTEND

class Platform::Impl
{
  public:
    auto InternAtom(std::string_view name, bool onlyIfExists) -> xcb_atom_t;
    void Init(const PlatformInitDesc &desc);
    void WinOpen();
    auto WinGetSize() -> PlatformWindowSize;
    auto PollEvent(PlatformEvent &outEvent) -> bool;

    auto Spawn(std::span<const char *const> cmd) -> bool;

    auto PageAlloc(uint32_t &inOutSize, void *&outBase) -> bool;

    auto GetMonotonicTimeMillis() -> uint64_t;
    auto GetMonotonicTimeMicros() -> uint64_t;
    auto GetMonotonicTimeNanos() -> uint64_t;

    auto WinGetHandle() -> xcb_window_t
    {
        return m_Win;
    }

    auto CreateWin(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask) -> xcb_window_t;

    auto GetScreenIndex() -> auto
    {
        return m_ScreenIndex;
    }

    auto GetScreen() -> auto *
    {
        return m_Screen;
    }

    auto GetRoot() -> xcb_window_t
    {
        return m_Screen->root;
    }

    auto GetConn() -> auto *
    {
        return m_Conn;
    }

    auto GetAtoms() -> auto &
    {
        return m_Atoms;
    }

    auto GetXInputExtensionMajorOpCode()
    {
        return m_ExtensionXInput2MajorOpCode;
    }

    void Grab()
    {
        xcb_grab_server(m_Conn);
    }

    void Ungrab()
    {
        xcb_ungrab_server(m_Conn);
    }

    void Flush()
    {
        xcb_flush(m_Conn);
    }

    void SetWindow(xcb_window_t window)
    {
        m_Win = window;
    }

    auto KeyPhysicalToKeyCode(KeyPhysical key) -> uint32_t
    {
        return m_KeyPhysicalCodes[static_cast<uint32_t>(key)];
    }

    auto KeyCodeToKeyPhysical(uint32_t keyCode, KeyPhysical *outKeyPhysical) -> bool
    {
        for (uint32_t i = 1; i < m_KeyPhysicalCodes.size(); ++i)
        {
            if (m_KeyPhysicalCodes[i] == keyCode)
            {
                *outKeyPhysical = static_cast<KeyPhysical>(i);
                return true;
            }
        }
        return false;
    }

    //

    void SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                             uint32_t arg4);
    void SendWmTakeFocus(xcb_window_t window, uint32_t time);
    void SendWmDeleteWindow(xcb_window_t window);
    void SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                             uint16_t height, uint16_t borderWidth);

  private:
    int m_ScreenIndex{};
    xcb_connection_t *m_Conn{};
    xcb_screen_t *m_Screen{};
    uint32_t m_ExtensionXInput2MajorOpCode{};

    xcb_window_t m_Win{};
    xcb_get_geometry_reply_t m_WinGeom{};

    struct
    {
#define X(name) xcb_atom_t name;
        NYLA_X11_ATOMS(X)
#undef X
    } m_Atoms;

    std::array<xcb_keycode_t, static_cast<uint32_t>(KeyPhysical::Count)> m_KeyPhysicalCodes;
};

//
} // namespace nyla