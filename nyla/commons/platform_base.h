#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/macros.h"

namespace nyla
{

enum class KeyPhysical;

enum class PlatformFeature
{
    Gfx = 1 << 0,
    KeyboardInput = 1 << 1,
    MouseInput = 1 << 2,
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
    bool open;
};

enum class FileOpenMode
{
    Read = 1 << 0,
    Write = 1 << 1,
    Append = 1 << 2,
};
NYLA_BITENUM(FileOpenMode);

using FileHandle = void *;

namespace Platform
{

constexpr inline uint64_t kPageAllocMinSize = 64_KiB;

auto NYLA_API GetMemPageSize() -> uint64_t;
auto NYLA_API ReserveMemPages(uint64_t size) -> char *;
void NYLA_API CommitMemPages(void *page, uint64_t size);
void NYLA_API DecommitMemPages(void *page, uint64_t size);

auto NYLA_API GetMonotonicTimeMillis() -> uint64_t;
auto NYLA_API GetMonotonicTimeMicros() -> uint64_t;
auto NYLA_API GetMonotonicTimeNanos() -> uint64_t;

auto NYLA_API Spawn(const char *const cmd, uint64_t count) -> bool;

void NYLA_API Init(const PlatformInitDesc &desc);
void NYLA_API WinOpen();
auto NYLA_API WinGetSize() -> PlatformWindowSize;
auto NYLA_API WinPollEvent(PlatformEvent &outEvent) -> bool;

auto NYLA_API UpdateGamepad(uint32_t index) -> bool;
void NYLA_API GetGamepadLeftStick(uint32_t index, float &outX, float &outY);
void NYLA_API GetGamepadRightStick(uint32_t index, float &outX, float &outY);
auto NYLA_API GetGamepadLeftTrigger(uint32_t index) -> float;
auto NYLA_API GetGamepadRightTrigger(uint32_t index) -> float;

auto NYLA_API GetStdin() -> FileHandle;
auto NYLA_API GetStdout() -> FileHandle;
auto NYLA_API GetStderr() -> FileHandle;

auto NYLA_API FileValid(FileHandle file) -> bool;
auto NYLA_API FileOpen(const char *path, FileOpenMode mode) -> FileHandle;
void NYLA_API FileClose(FileHandle file);
auto NYLA_API FileRead(FileHandle file, uint32_t size, uint8_t *out) -> uint32_t;
auto NYLA_API FileWrite(FileHandle file, uint32_t size, const char *in) -> uint32_t;
void NYLA_API FileSeek(FileHandle file, int64_t at);
auto NYLA_API FileTell(FileHandle file) -> uint64_t;

}; // namespace Platform

enum class KeyPhysical
{
    Unknown,

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

    Count,
};

} // namespace nyla