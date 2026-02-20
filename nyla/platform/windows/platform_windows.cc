#include "nyla/platform/windows/platform_windows.h"

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/containers/inline_ring.h"
#include "nyla/platform/platform.h"
#include "nyla/platform/platform_gamepad.h"
#include "nyla/platform/windows/platform_windows_gamepad.h"
#include <cstdint>

auto CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

namespace nyla
{

constexpr auto ScanCodeToKeyPhysical(uint8_t scanCode, bool extended) -> KeyPhysical;

void Platform::Impl::Init(const PlatformInitDesc &desc)
{
    GetSystemInfo(&m_SysInfo);

    m_AddressSpaceSize = AlignedUp<uint64_t>(256_GiB, m_SysInfo.dwAllocationGranularity);

    m_AddressSpaceBase = (char *)VirtualAlloc(nullptr, m_AddressSpaceSize, MEM_RESERVE, PAGE_NOACCESS);
    m_AddressSpaceAt = m_AddressSpaceBase;

    if (desc.open)
        WinOpen();
}

auto Platform::Impl::PollEvent(PlatformEvent &outEvent) -> bool
{
    while (m_EventsRing.empty() && !(m_Flags & (kFlagRepaint | kFlagRepaint | kFlagQuit)))
    {
        MSG msg{};
        if (!PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            break;

        if (msg.message == WM_QUIT)
        {
            m_Flags |= kFlagQuit;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (m_Flags & kFlagQuit)
    {
        outEvent = PlatformEvent{.type = PlatformEventType::Quit};
        return true;
    }

    if (m_Flags & kFlagWinResize)
    {
        outEvent = PlatformEvent{.type = PlatformEventType::WinResize};
        m_Flags &= ~kFlagWinResize;
        return true;
    }

    if (m_Flags & kFlagRepaint)
    {
        PAINTSTRUCT ps{};
        BeginPaint(m_HWnd, &ps);
        EndPaint(m_HWnd, &ps);

        outEvent = PlatformEvent{.type = PlatformEventType::Repaint};
        m_Flags &= ~kFlagRepaint;
        return true;
    }

    if (!m_EventsRing.empty())
    {
        outEvent = m_EventsRing.pop_front();
        return true;
    }

    return false;
}

auto Platform::Impl::WinGetHandle() -> HWND
{
    NYLA_ASSERT(m_HWnd);
    return m_HWnd;
}

void Platform::Impl::WinOpen()
{
    if (!m_HWnd)
    {
        constexpr wchar_t kName[] = L"nyla";

        WNDCLASSW windowClass = {
            .lpfnWndProc = ::MainWndProc,
            .hInstance = m_HInstance,
            .lpszClassName = kName,
        };
        RegisterClassW(&windowClass);

        m_HWnd = CreateWindowExW(0, kName, kName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 CW_USEDEFAULT, nullptr, nullptr, m_HInstance, nullptr);
    }

    ShowWindow(m_HWnd, SW_SHOW);
    UpdateWindow(m_HWnd);

    GetWindowRect(m_HWnd, &m_WinRect);
}

auto Platform::Impl::WinGetSize() -> PlatformWindowSize
{
    return {
        .width = static_cast<uint32_t>(m_WinRect.right - m_WinRect.left),
        .height = static_cast<uint32_t>(m_WinRect.bottom - m_WinRect.top),
    };
}

auto Platform::Impl::GetHInstance() -> HINSTANCE
{
    return m_HInstance;
}

void Platform::Impl::SetHInstance(HINSTANCE hInstance)
{
    m_HInstance = hInstance;
}

auto Platform::Impl::GetGamePad() -> XInputGamePad *
{
    static XInputGamePad ret = [] {
        XInputGamePad gamepad;
        gamepad.SetGamePad();
        return gamepad;
    }();
    return &ret;
}

//

void Platform::Init(const PlatformInitDesc &desc)
{
    NYLA_ASSERT(m_Impl);
    m_Impl->Init(desc);
}

void Platform::WinOpen()
{
    m_Impl->WinOpen();
}

auto Platform::WinGetSize() -> PlatformWindowSize
{
    return m_Impl->WinGetSize();
}

auto Platform::PollEvent(PlatformEvent &outEvent) -> bool
{
    return m_Impl->PollEvent(outEvent);
}

auto Platform::GetGamePad() -> GamePad *
{
    return m_Impl->GetGamePad();
}

LRESULT CALLBACK Platform::Impl::MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT: {
        m_Flags |= kFlagRepaint;
        return 0;
    }

    case WM_CLOSE: {
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY: {
        m_Flags |= kFlagQuit;
        PostQuitMessage(0);
        return 0;
    }

    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED)
            return 0;

        m_Flags |= kFlagWinResize;
        RECT newRect{};
        GetWindowRect(m_HWnd, &newRect);

        return 0;
    }

        // case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        UINT scanCode = (UINT)((lParam >> 16) & 0xFF);
        bool extended = ((lParam >> 24) & 0x01) != 0;
        bool wasDown = ((lParam >> 30) & 0x01) != 0;

        if (!wasDown)
        {
            KeyPhysical key = ScanCodeToKeyPhysical(scanCode, extended);
            m_EventsRing.emplace_back(PlatformEvent{
                .type = PlatformEventType::KeyDown,
                .key = key,
            });
        }
        return 0;
    }

        // case WM_SYSKEYUP:
    case WM_KEYUP: {
        UINT scanCode = (UINT)((lParam >> 16) & 0xFF);
        bool extended = ((lParam >> 24) & 0x01) != 0;

        KeyPhysical key = ScanCodeToKeyPhysical(scanCode, extended);
        m_EventsRing.emplace_back(PlatformEvent{
            .type = PlatformEventType::KeyUp,
            .key = key,
        });
        return 0;
    }

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

constexpr auto ScanCodeToKeyPhysical(uint8_t scanCode, bool extended) -> KeyPhysical
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

Platform g_Platform{};

} // namespace nyla

auto WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) -> int
{
    using namespace nyla;

#ifndef NDEBUG
    NYLA_ASSERT(AllocConsole());

    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
#endif

    auto *impl = new Platform::Impl{};
    impl->SetHInstance(hInstance);
    g_Platform.SetImpl(impl);

    const int retCode = PlatformMain();

#ifndef NDEBUG
    getc(stdin);
#endif

    return retCode;
}

auto CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT
{
    return nyla::g_Platform.GetImpl()->MainWndProc(hwnd, uMsg, wParam, lParam);
}