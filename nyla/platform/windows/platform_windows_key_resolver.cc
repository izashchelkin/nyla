#include "nyla/platform/platform_key_resolver.h"
#include "nyla/platform/platform.h"

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


} // namespace nyla