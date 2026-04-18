#include "nyla/commons/platform_windows.h"

#include <cstdint>
#include <immintrin.h>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/gamepad.h"
#include "nyla/commons/inline_queue.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/span.h"
#include "nyla/commons/time.h"

auto CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

namespace nyla
{

namespace
{

SYSTEM_INFO g_SysInfo{};
HINSTANCE g_HInstance{};
HWND g_HWnd{};
RECT g_WinRect{};

array<XINPUT_STATE, 1> g_Gamepads{};

static inline constexpr uint32_t kFlagQuit = 1 << 0;
static inline constexpr uint32_t kFlagWinResize = 1 << 1;
static inline constexpr uint32_t kFlagRepaint = 1 << 2;
uint32_t g_Flags{};

inline_queue<PlatformEvent, 32> g_EventsQueue{};

} // namespace

namespace
{

auto GetPerformanceFreq() -> uint64_t
{
    static uint64_t freq = 0;
    if (freq == 0)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return static_cast<uint64_t>(freq.QuadPart);
    };
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
    uint64_t hi;
    uint64_t lo = UMul128(ticks, scale, hi);
    uint64_t rem;
    return UDiv128(hi, lo, GetPerformanceFreq(), rem);
}

} // namespace

auto API GetMonotonicTimeMillis() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000ULL);
}

auto API GetMonotonicTimeMicros() -> uint64_t
{
    return TicksTo(GetPerformanceTicks(), 1'000'000ULL);
}

void API Sleep(uint64_t millis)
{
    ::Sleep((DWORD)millis);
}

auto API ReserveMemPages(uint64_t size) -> void *
{
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

void API CommitMemPages(void *page, uint64_t size)
{
    VirtualAlloc(page, size, MEM_COMMIT, PAGE_READWRITE);
}

void API DecommitMemPages(void *page, uint64_t size)
{
    VirtualAlloc(page, size, MEM_DECOMMIT, PAGE_NOACCESS);
}

auto API WinPollEvent(PlatformEvent &outEvent) -> bool
{
    for (;;)
    {
        if (InlineQueue::IsEmpty(g_EventsQueue))
            break;
        if (g_Flags & kFlagRepaint)
            return true;
        if (g_Flags & kFlagWinResize)
            return true;
        if (g_Flags & kFlagQuit)
            return true;

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

    if (!InlineQueue::IsEmpty(g_EventsQueue))
    {
        outEvent = InlineQueue::Read(g_EventsQueue);
        return true;
    }

    return false;
}

auto WindowsPlatform::WinGetHandle() -> HWND
{
    ASSERT(g_HWnd);
    return g_HWnd;
}

void API WinOpen()
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

auto API WinGetSize() -> PlatformWindowSize
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

auto UpdateGamepad(uint32_t index) -> bool
{
    auto &state = g_Gamepads[index];
    MemZero(&state.Gamepad);

    const DWORD dwResult = XInputGetState(0, &state);
    return (dwResult == ERROR_SUCCESS);
}

namespace
{

void GetGamepadStick(auto rawX, auto rawY, auto rawDeadzone, float &outX, float &outY)
{
    const float x = (float)rawX / (float)Limits<int16_t>::Max();
    const float y = (float)rawY / (float)Limits<int16_t>::Max();

    const float magnitude = Sqrt(x * x + y * y);
    const float deadzone = (float)rawDeadzone / (float)Limits<int16_t>::Max();

    if (magnitude < deadzone)
    {
        outX = 0.f;
        outY = 0.f;
    }
    else
    { // Renormalize so movement starts smoothly from the edge of the deadzone
        const float scale = (magnitude - deadzone) / (1.0f - deadzone);

        outX = (x / magnitude) * scale;
        outY = (y / magnitude) * scale;
    }
}

auto GetGamepadTrigger(uint8_t rawValue, uint8_t rawDeadzone) -> float
{
    const float val = (float)rawValue / (float)Limits<uint8_t>::Max();
    const float deadzone = (float)rawDeadzone / (float)Limits<uint8_t>::Max();

    if (val < deadzone)
    {
        return 0.0f;
    }

    // Renormalize so movement starts smoothly from the edge of the deadzone
    return (val - deadzone) / (1.0f - deadzone);
}

} // namespace

auto API GetGamepadLeftStick(uint32_t index) -> float2
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    float2 ret;
    GetGamepadStick(gamepad.sThumbLX, gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, ret[0], ret[1]);
    return ret;
}

auto API GetGamepadRightStick(uint32_t index) -> float2
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    float2 ret;
    GetGamepadStick(gamepad.sThumbRX, gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, ret[0], ret[1]);
    return ret;
}

auto API GetGamepadLeftTrigger(uint32_t index) -> float
{
    auto &gamepad = g_Gamepads[index].Gamepad;
    return GetGamepadTrigger(gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

auto API GetGamepadRightTrigger(uint32_t index) -> float
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
            InlineQueue::Write(g_EventsQueue, PlatformEvent{
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
        InlineQueue::Write(g_EventsQueue, PlatformEvent{
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

auto FileValid(file_handle file) -> bool
{
    auto hFile = reinterpret_cast<HANDLE>(file);
    return hFile != nullptr && hFile != INVALID_HANDLE_VALUE;
}

auto FileOpen(byteview path, FileOpenMode mode) -> file_handle
{
    DWORD dwDesiredAccess = 0;
    DWORD dwCreationDisposition = 0;

    if (Any(mode & FileOpenMode::Read))
    {
        dwDesiredAccess |= GENERIC_READ;
        dwCreationDisposition = OPEN_EXISTING;
    }

    if (Any(mode & FileOpenMode::Append))
    {
        dwDesiredAccess |= GENERIC_WRITE;
        dwCreationDisposition = OPEN_ALWAYS;
    }
    if (Any(mode & FileOpenMode::Write))
    {
        dwDesiredAccess |= GENERIC_WRITE;
        dwCreationDisposition = CREATE_ALWAYS;
    }

    auto hFile = CreateFileA(Span::CStr(path), // lpFileName
                             dwDesiredAccess,
                             FILE_SHARE_READ, // dwShareMode
                             nullptr,         // lpSecurityAttributes
                             dwCreationDisposition,
                             FILE_ATTRIBUTE_NORMAL, // dwFlagsAndAttributes
                             nullptr                // hTemplateFile
    );

    if (hFile && hFile != INVALID_HANDLE_VALUE)
    {
        if (Any(mode & FileOpenMode::Append))
        {
            LARGE_INTEGER distanceToMove{};
            SetFilePointerEx(hFile, distanceToMove, nullptr, FILE_END);
        }
    }

    return reinterpret_cast<void *>(hFile);
}

void FileClose(file_handle file)
{
    if (!FileValid(file))
        return;

    auto hFile = reinterpret_cast<HANDLE>(file);
    CloseHandle(hFile);
}

auto FileRead(file_handle file, uint32_t size, uint8_t *out) -> uint32_t
{
    auto hFile = reinterpret_cast<HANDLE>(file);
    DWORD bytesRead = 0;

    ReadFile(hFile,      // hFile
             out,        // lpBuffer
             size,       // nNumberOfBytesToRead
             &bytesRead, // lpNumberOfBytesRead
             nullptr     // lpOverlapped
    );
    return bytesRead;
}

auto FileWrite(file_handle file, uint32_t size, const uint8_t *in) -> uint32_t
{
    auto hFile = reinterpret_cast<HANDLE>(file);
    DWORD bytesWritten = 0;

    WriteFile(hFile,         // hFile
              in,            // lpBuffer
              size,          // nNumberOfBytesToWrite
              &bytesWritten, // lpNumberOfBytesWritten
              nullptr        // lpOverlapped
    );
    return bytesWritten;
}

void FileSeek(file_handle file, int64_t at)
{
    auto hFile = reinterpret_cast<HANDLE>(file);

    LARGE_INTEGER distanceToMove;
    distanceToMove.QuadPart = at;

    SetFilePointerEx(hFile,          // hFile
                     distanceToMove, // lDistanceToMove
                     nullptr,        // lpDistanceToMoveHigh
                     FILE_BEGIN      // dwMoveMethod
    );
}

auto FileTell(file_handle file) -> uint64_t
{
    auto hFile = reinterpret_cast<HANDLE>(file);

    LARGE_INTEGER distanceToMove{};
    LARGE_INTEGER newFilePointer{};

    if (SetFilePointerEx(hFile, distanceToMove, &newFilePointer, FILE_CURRENT))
    {
        return newFilePointer.QuadPart;
    }
    else
    { // failed
        ASSERT(false);
        return 0ULL;
    }
}

auto GetStdin() -> file_handle
{
    return GetStdHandle(STD_INPUT_HANDLE);
}

auto GetStdout() -> file_handle
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

auto GetStderr() -> file_handle
{
    return GetStdHandle(STD_ERROR_HANDLE);
}

void ParseStdArgs(byteview *args, uint32_t maxArgs)
{
    uint8_t *cmdLine = (uint8_t *)GetCommandLineA();
    uint64_t cmdLineLen = CStrLen(cmdLine, 256);

    const uint8_t *cursor = cmdLine;
    bool inQuotes = false;
    uint32_t argCount = 0;

    const uint8_t *argStart = nullptr;

    for (uint64_t i = 0; i < cmdLineLen && argCount < maxArgs; ++i)
    {
        char ch = cmdLine[i];

        if (ch == '"')
        {
            inQuotes = !inQuotes;
            if (inQuotes)
                argStart = &cmdLine[i + 1]; // Start after the quote
            else
            {
                // Closing quote: Save the string between argStart and here
                args[argCount++] = byteview{argStart, (uint32_t)(&cmdLine[i] - argStart)};
                argStart = nullptr;
            }
        }
        else if (!inQuotes)
        {
            if (ch == ' ')
            {
                if (argStart) // We were in a word, and just hit a space
                {
                    args[argCount++] = byteview{argStart, (uint32_t)(&cmdLine[i] - argStart)};
                    argStart = nullptr;
                }
            }
            else if (!argStart) // We weren't in a word, and just hit a character
            {
                argStart = &cmdLine[i];
            }
        }
    }

    // Capture the trailing argument if there was no trailing space/quote
    if (argStart && argCount < maxArgs)
    {
        args[argCount++] = byteview{argStart, (uint32_t)((cmdLine + cmdLineLen) - argStart)};
    }

    ASSERT(!inQuotes);
}

void PlatformBootstrap()
{
    GetNativeSystemInfo(&g_SysInfo);
    ASSERT(g_SysInfo.dwPageSize == kPageSize);

#ifndef NDEBUG
    ASSERT(AllocConsole());

    HANDLE hConOut =
        CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE hConIn =
        CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

    SetStdHandle(STD_INPUT_HANDLE, hConIn);
    SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
    SetStdHandle(STD_ERROR_HANDLE, hConOut);
#endif
}

void PlatformTearDown()
{
#ifndef NDEBUG
    HANDLE hConIn =
        CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

    for (;;)
    {
        INPUT_RECORD ir;
        DWORD read;
        if (ReadConsoleInputA(hConIn, &ir, 1, &read) && read > 0)
        {
            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown)
                break;
        }
    }

    PostMessage(GetConsoleWindow(), WM_CLOSE, 0, 0);
#endif
}

} // namespace nyla