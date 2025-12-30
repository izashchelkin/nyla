#include "nyla/platform/platform_key_resolver.h"

namespace nyla
{

struct WinScanCode
{
    uint8_t code = 0;
    bool extended = false;
};

constexpr WinScanCode KeyPhysicalToScanCode(KeyPhysical k);
constexpr KeyPhysical ScanCodeToKeyPhysical(uint8_t scanCode, bool extended);

class PlatformKeyResolver::Impl
{
};

void PlatformKeyResolver::Init()
{
}

void PlatformKeyResolver::Destroy()
{
}

auto PlatformKeyResolver::ResolveKeyCode(KeyPhysical key) -> uint32_t
{
    WinScanCode scanCode = KeyPhysicalToScanCode(key);
    return scanCode.code << 1 | static_cast<uint32_t>(scanCode.extended);
}

constexpr WinScanCode KeyPhysicalToScanCode(KeyPhysical k)
{
    switch (k)
    {
    case KeyPhysical::Escape:
        return {0x01, false};
    case KeyPhysical::Grave:
        return {0x29, false};
    case KeyPhysical::Digit1:
        return {0x02, false};
    case KeyPhysical::Digit2:
        return {0x03, false};
    case KeyPhysical::Digit3:
        return {0x04, false};
    case KeyPhysical::Digit4:
        return {0x05, false};
    case KeyPhysical::Digit5:
        return {0x06, false};
    case KeyPhysical::Digit6:
        return {0x07, false};
    case KeyPhysical::Digit7:
        return {0x08, false};
    case KeyPhysical::Digit8:
        return {0x09, false};
    case KeyPhysical::Digit9:
        return {0x0A, false};
    case KeyPhysical::Digit0:
        return {0x0B, false};
    case KeyPhysical::Minus:
        return {0x0C, false};
    case KeyPhysical::Equal:
        return {0x0D, false};
    case KeyPhysical::Backspace:
        return {0x0E, false};

    case KeyPhysical::Tab:
        return {0x0F, false};
    case KeyPhysical::Q:
        return {0x10, false};
    case KeyPhysical::W:
        return {0x11, false};
    case KeyPhysical::E:
        return {0x12, false};
    case KeyPhysical::R:
        return {0x13, false};
    case KeyPhysical::T:
        return {0x14, false};
    case KeyPhysical::Y:
        return {0x15, false};
    case KeyPhysical::U:
        return {0x16, false};
    case KeyPhysical::I:
        return {0x17, false};
    case KeyPhysical::O:
        return {0x18, false};
    case KeyPhysical::P:
        return {0x19, false};
    case KeyPhysical::LeftBracket:
        return {0x1A, false};
    case KeyPhysical::RightBracket:
        return {0x1B, false};
    case KeyPhysical::Enter:
        return {0x1C, false};

    case KeyPhysical::CapsLock:
        return {0x3A, false};
    case KeyPhysical::A:
        return {0x1E, false};
    case KeyPhysical::S:
        return {0x1F, false};
    case KeyPhysical::D:
        return {0x20, false};
    case KeyPhysical::F:
        return {0x21, false};
    case KeyPhysical::G:
        return {0x22, false};
    case KeyPhysical::H:
        return {0x23, false};
    case KeyPhysical::J:
        return {0x24, false};
    case KeyPhysical::K:
        return {0x25, false};
    case KeyPhysical::L:
        return {0x26, false};
    case KeyPhysical::Semicolon:
        return {0x27, false};
    case KeyPhysical::Apostrophe:
        return {0x28, false};

    case KeyPhysical::LeftShift:
        return {0x2A, false};
    case KeyPhysical::Z:
        return {0x2C, false};
    case KeyPhysical::X:
        return {0x2D, false};
    case KeyPhysical::C:
        return {0x2E, false};
    case KeyPhysical::V:
        return {0x2F, false};
    case KeyPhysical::B:
        return {0x30, false};
    case KeyPhysical::N:
        return {0x31, false};
    case KeyPhysical::M:
        return {0x32, false};
    case KeyPhysical::Comma:
        return {0x33, false};
    case KeyPhysical::Period:
        return {0x34, false};
    case KeyPhysical::Slash:
        return {0x35, false};
    case KeyPhysical::RightShift:
        return {0x36, false};

    case KeyPhysical::LeftCtrl:
        return {0x1D, false};
    case KeyPhysical::LeftAlt:
        return {0x38, false};
    case KeyPhysical::Space:
        return {0x39, false};
    case KeyPhysical::RightAlt:
        return {0x38, true}; // AltGr (E0 38)
    case KeyPhysical::RightCtrl:
        return {0x1D, true}; // E0 1D

    case KeyPhysical::ArrowLeft:
        return {0x4B, true}; // E0 4B
    case KeyPhysical::ArrowRight:
        return {0x4D, true}; // E0 4D
    case KeyPhysical::ArrowUp:
        return {0x48, true}; // E0 48
    case KeyPhysical::ArrowDown:
        return {0x50, true}; // E0 50

    case KeyPhysical::F1:
        return {0x3B, false};
    case KeyPhysical::F2:
        return {0x3C, false};
    case KeyPhysical::F3:
        return {0x3D, false};
    case KeyPhysical::F4:
        return {0x3E, false};
    case KeyPhysical::F5:
        return {0x3F, false};
    case KeyPhysical::F6:
        return {0x40, false};
    case KeyPhysical::F7:
        return {0x41, false};
    case KeyPhysical::F8:
        return {0x42, false};
    case KeyPhysical::F9:
        return {0x43, false};
    case KeyPhysical::F10:
        return {0x44, false};
    case KeyPhysical::F11:
        return {0x57, false};
    case KeyPhysical::F12:
        return {0x58, false};

    default:
        return {0x00, false};
    }
}

constexpr KeyPhysical ScanCodeToKeyPhysical(uint8_t scanCode, bool extended)
{
    // Extended (E0) keys first where collisions exist.
    if (extended)
    {
        switch (scanCode)
        {
        case 0x38:
            return KeyPhysical::RightAlt;
        case 0x1D:
            return KeyPhysical::RightCtrl;

        case 0x4B:
            return KeyPhysical::ArrowLeft;
        case 0x4D:
            return KeyPhysical::ArrowRight;
        case 0x48:
            return KeyPhysical::ArrowUp;
        case 0x50:
            return KeyPhysical::ArrowDown;

        default:
            return KeyPhysical::Unknown;
        }
    }

    switch (scanCode)
    {
    case 0x01:
        return KeyPhysical::Escape;
    case 0x29:
        return KeyPhysical::Grave;
    case 0x02:
        return KeyPhysical::Digit1;
    case 0x03:
        return KeyPhysical::Digit2;
    case 0x04:
        return KeyPhysical::Digit3;
    case 0x05:
        return KeyPhysical::Digit4;
    case 0x06:
        return KeyPhysical::Digit5;
    case 0x07:
        return KeyPhysical::Digit6;
    case 0x08:
        return KeyPhysical::Digit7;
    case 0x09:
        return KeyPhysical::Digit8;
    case 0x0A:
        return KeyPhysical::Digit9;
    case 0x0B:
        return KeyPhysical::Digit0;
    case 0x0C:
        return KeyPhysical::Minus;
    case 0x0D:
        return KeyPhysical::Equal;
    case 0x0E:
        return KeyPhysical::Backspace;

    case 0x0F:
        return KeyPhysical::Tab;
    case 0x10:
        return KeyPhysical::Q;
    case 0x11:
        return KeyPhysical::W;
    case 0x12:
        return KeyPhysical::E;
    case 0x13:
        return KeyPhysical::R;
    case 0x14:
        return KeyPhysical::T;
    case 0x15:
        return KeyPhysical::Y;
    case 0x16:
        return KeyPhysical::U;
    case 0x17:
        return KeyPhysical::I;
    case 0x18:
        return KeyPhysical::O;
    case 0x19:
        return KeyPhysical::P;
    case 0x1A:
        return KeyPhysical::LeftBracket;
    case 0x1B:
        return KeyPhysical::RightBracket;
    case 0x1C:
        return KeyPhysical::Enter;

    case 0x3A:
        return KeyPhysical::CapsLock;
    case 0x1E:
        return KeyPhysical::A;
    case 0x1F:
        return KeyPhysical::S;
    case 0x20:
        return KeyPhysical::D;
    case 0x21:
        return KeyPhysical::F;
    case 0x22:
        return KeyPhysical::G;
    case 0x23:
        return KeyPhysical::H;
    case 0x24:
        return KeyPhysical::J;
    case 0x25:
        return KeyPhysical::K;
    case 0x26:
        return KeyPhysical::L;
    case 0x27:
        return KeyPhysical::Semicolon;
    case 0x28:
        return KeyPhysical::Apostrophe;

    case 0x2A:
        return KeyPhysical::LeftShift;
    case 0x2C:
        return KeyPhysical::Z;
    case 0x2D:
        return KeyPhysical::X;
    case 0x2E:
        return KeyPhysical::C;
    case 0x2F:
        return KeyPhysical::V;
    case 0x30:
        return KeyPhysical::B;
    case 0x31:
        return KeyPhysical::N;
    case 0x32:
        return KeyPhysical::M;
    case 0x33:
        return KeyPhysical::Comma;
    case 0x34:
        return KeyPhysical::Period;
    case 0x35:
        return KeyPhysical::Slash;
    case 0x36:
        return KeyPhysical::RightShift;

    case 0x1D:
        return KeyPhysical::LeftCtrl;
    case 0x38:
        return KeyPhysical::LeftAlt;
    case 0x39:
        return KeyPhysical::Space;

    case 0x3B:
        return KeyPhysical::F1;
    case 0x3C:
        return KeyPhysical::F2;
    case 0x3D:
        return KeyPhysical::F3;
    case 0x3E:
        return KeyPhysical::F4;
    case 0x3F:
        return KeyPhysical::F5;
    case 0x40:
        return KeyPhysical::F6;
    case 0x41:
        return KeyPhysical::F7;
    case 0x42:
        return KeyPhysical::F8;
    case 0x43:
        return KeyPhysical::F9;
    case 0x44:
        return KeyPhysical::F10;
    case 0x57:
        return KeyPhysical::F11;
    case 0x58:
        return KeyPhysical::F12;

    default:
        return KeyPhysical::Unknown;
    }
}

} // namespace nyla