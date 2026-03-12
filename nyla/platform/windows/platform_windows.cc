#include "nyla/platform/windows/platform_windows.h"

#include <cstdint>
#include <immintrin.h>
#include <limits>
#include <span>

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/containers/inline_ring.h"
#include "nyla/platform/platform.h"
#include "platform_windows.h"

auto CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

namespace nyla
{

namespace
{

char *g_AddressSpaceBase;
char *g_AddressSpaceAt;
uint64_t g_AddressSpaceSize;

SYSTEM_INFO g_SysInfo{};
HINSTANCE g_HInstance{};
HWND g_HWnd{};
RECT g_WinRect{};

std::array<XINPUT_STATE, 1> g_Gamepads{};

static inline constexpr uint32_t kFlagQuit = 1 << 0;
static inline constexpr uint32_t kFlagWinResize = 1 << 1;
static inline constexpr uint32_t kFlagRepaint = 1 << 2;
uint32_t g_Flags{};

InlineRing<PlatformEvent, 32> g_EventsRing{};

} // namespace

namespace
{

auto GetPerformanceFreq() -> uint64_t
{
    static const auto freq = [] -> uint64_t {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return static_cast<uint64_t>(freq.QuadPart);
    }();
    return freq;
}

auto GetPerformanceTicks() -> uint64_t
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
}

auto TicksTo(uint64_t ticks, uint64_t scale) -> uint64_t
{
    unsigned __int64 hi = 0;
    unsigned __int64 lo = _umul128(ticks, scale, &hi);

    unsigned __int64 rem = 0;
    unsigned __int64 q = _udiv128(hi, lo, GetPerformanceFreq(), &rem);
    return static_cast<uint64_t>(q);
}

} // namespace

auto Platform::GetMonotonicTimeMillis() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000ULL);
}

auto Platform::GetMonotonicTimeMicros() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000'000ULL);
}

auto Platform::GetMemPageSize() -> uint64_t
{
    return g_SysInfo.dwPageSize;
}

auto Platform::ReserveMemPages(uint64_t size) -> char *
{
    char *ret = g_AddressSpaceAt;
    g_AddressSpaceAt += AlignedUp<uint64_t>(size, g_SysInfo.dwPageSize);
    return ret;
}

void Platform::CommitMemPages(char *page, uint64_t size)
{
    NYLA_ASSERT(((page - g_AddressSpaceBase) % g_SysInfo.dwPageSize) == 0);

    AlignUp<uint64_t>(size, g_SysInfo.dwPageSize);

    VirtualAlloc(page, size, MEM_COMMIT, PAGE_READWRITE);
}

void Platform::DecommitMemPages(char *page, uint64_t size)
{
    NYLA_ASSERT(((page - g_AddressSpaceBase) % g_SysInfo.dwPageSize) == 0);

    AlignUp<uint64_t>(size, g_SysInfo.dwPageSize);

    VirtualAlloc(page, size, MEM_DECOMMIT, PAGE_NOACCESS);
}

void Platform::InitGraphical(const PlatformInitDesc &desc)
{
    GetSystemInfo(&g_SysInfo);

    g_AddressSpaceSize = AlignedUp<uint64_t>(256_GiB, g_SysInfo.dwAllocationGranularity);

    g_AddressSpaceBase = (char *)VirtualAlloc(nullptr, g_AddressSpaceSize, MEM_RESERVE, PAGE_NOACCESS);
    g_AddressSpaceAt = g_AddressSpaceBase;

    if (desc.open)
        WinOpen();
}

auto Platform::WinPollEvent(PlatformEvent &outEvent) -> bool
{
    while (g_EventsRing.empty() && !(g_Flags & (kFlagRepaint | kFlagRepaint | kFlagQuit)))
    {
        MSG msg{};
        if (!PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            break;

        if (msg.message == WM_QUIT)
        {
            g_Flags |= kFlagQuit;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_Flags & kFlagQuit)
    {
        outEvent = PlatformEvent{.type = PlatformEventType::Quit};
        return true;
    }

    if (g_Flags & kFlagWinResize)
    {
        outEvent = PlatformEvent{.type = PlatformEventType::WinResize};
        g_Flags &= ~kFlagWinResize;
        return true;
    }

    if (g_Flags & kFlagRepaint)
    {
        PAINTSTRUCT ps{};
        BeginPaint(g_HWnd, &ps);
        EndPaint(g_HWnd, &ps);

        outEvent = PlatformEvent{.type = PlatformEventType::Repaint};
        g_Flags &= ~kFlagRepaint;
        return true;
    }

    if (!g_EventsRing.empty())
    {
        outEvent = g_EventsRing.pop_front();
        return true;
    }

    return false;
}

auto WindowsPlatform::WinGetHandle() -> HWND
{
    NYLA_ASSERT(g_HWnd);
    return g_HWnd;
}

void Platform::WinOpen()
{
    if (!g_HWnd)
    {
        constexpr wchar_t kName[] = L"nyla";

        WNDCLASSW windowClass = {
            .lpfnWndProc = WindowsPlatform::MainWndProc,
            .hInstance = g_HInstance,
            .lpszClassName = kName,
        };
        RegisterClassW(&windowClass);

        g_HWnd = CreateWindowExW(0, kName, kName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 CW_USEDEFAULT, nullptr, nullptr, g_HInstance, nullptr);
    }

    ShowWindow(g_HWnd, SW_SHOW);
    UpdateWindow(g_HWnd);

    GetWindowRect(g_HWnd, &g_WinRect);
}

auto Platform::WinGetSize() -> PlatformWindowSize
{
    return {
        .width = static_cast<uint32_t>(g_WinRect.right - g_WinRect.left),
        .height = static_cast<uint32_t>(g_WinRect.bottom - g_WinRect.top),
    };
}

auto WindowsPlatform::GetHInstance() -> HINSTANCE
{
    return g_HInstance;
}

void WindowsPlatform::SetHInstance(HINSTANCE hInstance)
{
    g_HInstance = hInstance;
}

//

auto Platform::UpdateGamepad(uint32_t index) -> bool
{
    auto &state = g_Gamepads[index];
    ZeroMemory(&state.Gamepad, sizeof(state.Gamepad));

    const DWORD dwResult = XInputGetState(0, &state);
    return (dwResult == ERROR_SUCCESS);
}

namespace
{

auto GetGamepadStick(auto rawX, auto rawY, auto rawDeadzone) -> float2
{
    const float x = (float)rawX / (float)std::numeric_limits<int16_t>::max();
    const float y = (float)rawY / (float)std::numeric_limits<int16_t>::max();

    const float magnitude = std::sqrt(x * x + y * y);
    const float deadzone = (float)rawDeadzone / (float)std::numeric_limits<int16_t>::max();

    if (magnitude < deadzone)
    {
        return {0.0f, 0.0f};
    }

    // Renormalize so movement starts smoothly from the edge of the deadzone
    const float scale = (magnitude - deadzone) / (1.0f - deadzone);

    return {(x / magnitude) * scale, (y / magnitude) * scale};
}

auto GetGamepadTrigger(uint8_t rawValue, uint8_t rawDeadzone) -> float
{
    const float val = (float)rawValue / (float)std::numeric_limits<uint8_t>::max();
    const float deadzone = (float)rawDeadzone / (float)std::numeric_limits<uint8_t>::max();

    if (val < deadzone)
    {
        return 0.0f;
    }

    // Renormalize so movement starts smoothly from the edge of the deadzone
    return (val - deadzone) / (1.0f - deadzone);
}

} // namespace

auto Platform::GetGamepadLeftStick(uint32_t index) -> float2
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    return GetGamepadStick(gamepad.sThumbLX, gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

auto Platform::GetGamepadRightStick(uint32_t index) -> float2
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    return GetGamepadStick(gamepad.sThumbRX, gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
}

auto Platform::GetGamepadLeftTrigger(uint32_t index) -> float
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    return GetGamepadTrigger(gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

auto Platform::GetGamepadRightTrigger(uint32_t index) -> float
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    return GetGamepadTrigger(gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

auto CALLBACK WindowsPlatform::MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT
{
    switch (uMsg)
    {
    case WM_PAINT: {
        g_Flags |= kFlagRepaint;
        return 0;
    }

    case WM_CLOSE: {
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY: {
        g_Flags |= kFlagQuit;
        PostQuitMessage(0);
        return 0;
    }

    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED)
            return 0;

        g_Flags |= kFlagWinResize;
        RECT newRect{};
        GetWindowRect(g_HWnd, &newRect);

        return 0;
    }

        // case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        UINT scanCode = (UINT)((lParam >> 16) & 0xFF);
        bool extended = ((lParam >> 24) & 0x01) != 0;
        bool wasDown = ((lParam >> 30) & 0x01) != 0;

        if (!wasDown)
        {
            KeyPhysical key = WindowsPlatform::ScanCodeToKeyPhysical(scanCode, extended);
            g_EventsRing.emplace_back(PlatformEvent{
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

        KeyPhysical key = WindowsPlatform::ScanCodeToKeyPhysical(scanCode, extended);
        g_EventsRing.emplace_back(PlatformEvent{
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

auto WindowsPlatform::ScanCodeToKeyPhysical(uint8_t scanCode, bool extended) -> KeyPhysical
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

    const int retCode = PlatformMain(std::span<const char *>{(const char **)__argv, (uint32_t)__argc});

#ifndef NDEBUG
    getc(stdin);
#endif

    return retCode;
}