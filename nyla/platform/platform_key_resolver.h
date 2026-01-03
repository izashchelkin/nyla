#pragma once

#include <cstdint>
namespace nyla
{

enum class KeyPhysical;

class PlatformKeyResolver
{
  public:
    void Init();
    void Destroy();

    auto ResolveKeyCode(KeyPhysical) -> uint32_t;

  private:
    class Impl;
    Impl *m_Impl{};
};

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