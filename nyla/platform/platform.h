#pragma once

#include "nyla/commons/bitenum.h"
#include <cstdint>

namespace nyla
{

enum class KeyPhysical;

enum class PlatformFeature
{
    KeyboardInput = 1 << 0,
    MouseInput = 1 << 1,
};
NYLA_BITENUM(PlatformFeature);

struct PlatformWindow
{
    std::uintptr_t handle;
};

struct PlatformWindowSize
{
    uint32_t width;
    uint32_t height;
};

enum class PlatformEventType
{
    None,

    KeyDown,
    KeyUp,
    MousePress,
    MouseRelease,

    WinResize,

    Repaint,
    Quit
};

struct PlatformEvent
{
    PlatformEventType type;
    union {
        KeyPhysical key;

        struct
        {
            uint32_t code;
        } mouse;
    };
};

struct PlatformInitDesc
{
    PlatformFeature enabledFeatures;
};

class Platform
{
  public:
    void Init(const PlatformInitDesc &desc);
    void WinOpen();
    auto WinGetSize() -> PlatformWindowSize;
    auto PollEvent(PlatformEvent &outEvent) -> bool;

    class Impl;

    void SetImpl(Impl *impl)
    {
        m_Impl = impl;
    }

    auto GetImpl() -> auto *
    {
        return m_Impl;
    }

  private:
    Impl *m_Impl;
};
extern Platform *g_Platform;

int PlatformMain();

enum class KeyPhysical
{
    Unknown = 0,

    Escape,
    Grave,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,
    Minus,
    Equal,
    Backspace,

    Tab,
    Q,
    W,
    E,
    R,
    T,
    Y,
    U,
    I,
    O,
    P,
    LeftBracket,
    RightBracket,
    Enter,

    CapsLock,
    A,
    S,
    D,
    F,
    G,
    H,
    J,
    K,
    L,
    Semicolon,
    Apostrophe,

    LeftShift,
    Z,
    X,
    C,

    V,
    B,
    N,
    M,
    Comma,
    Period,
    Slash,
    RightShift,

    LeftCtrl,
    LeftAlt,
    Space,
    RightAlt,
    RightCtrl,

    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

} // namespace nyla