#pragma once

#include "nyla/commons/bitenum.h"
#include <cstdint>

namespace nyla
{

enum class PlatformFeature
{
    KeyboardInput,
    MouseInput,
};
NYLA_BITENUM(PlatformFeature);

struct PlatformWindow
{
    uint32_t handle;
};

struct PlatformWindowSize
{
    uint32_t width;
    uint32_t height;
};

enum class PlatformEventType
{
    None,

    KeyPress,
    KeyRelease,
    MousePress,
    MouseRelease,

    ShouldRedraw,
    ShouldExit
};

struct PlatformEvent
{
    PlatformEventType type;
    union {
        struct
        {
            uint32_t code;
        } key;

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

struct PlatformImpl
{
    void *data;
    void (*init)(void *data, const PlatformInitDesc &);
    PlatformWindow (*createWindow)(void *data);
    PlatformWindowSize (*getWindowSize)(void *data, PlatformWindow);
    bool (*pollEvent)(void *data, PlatformEvent &outEvent);
};
extern PlatformImpl *g_PlatformImpl;

class Platform
{
  public:
    Platform(PlatformImpl *impl) : m_Impl{impl} {};

    void Init(const PlatformInitDesc &desc)
    {
        m_Impl->init(m_Impl->data, desc);
    }

    auto CreateWindow()
    {
        return m_Impl->createWindow(m_Impl->data);
    }

    auto GetWindowSize(PlatformWindow window)
    {
        return m_Impl->getWindowSize(m_Impl->data, window);
    }

    auto PollEvent(PlatformEvent &outEvent)
    {
        return m_Impl->pollEvent(m_Impl->data, outEvent);
    }

  private:
    PlatformImpl *m_Impl;
};
extern Platform *g_Platform;

//

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