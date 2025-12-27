#pragma once

#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include <string_view>
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
    auto CreateWindow() -> PlatformWindow;
    auto GetWindowSize(PlatformWindow platformWindow) -> PlatformWindowSize;
    auto PollEvent(PlatformEvent &outEvent) -> bool;

    auto CreateWindow(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask)
        -> xcb_window_t;

    auto GetScreenIndex() -> auto
    {
        return m_ScreenIndex;
    }

    auto GetScreen() -> auto *
    {
        return m_Screen;
    }

    auto GetConn() -> auto *
    {
        return m_Conn;
    }

    auto GetAtoms() -> auto &
    {
        return m_Atoms;
    }

  private:
    int m_ScreenIndex;
    xcb_connection_t *m_Conn;
    xcb_screen_t *m_Screen;

    struct
    {
#define X(name) xcb_atom_t name;
        NYLA_X11_ATOMS(X)
#undef X
    } m_Atoms;
};

//
} // namespace nyla