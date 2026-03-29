#pragma once

#include <cstdint>

#include "nyla/commons/assert.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/dllapi.h"
#include "nyla/commons/path.h"
#include "nyla/commons/span.h"
#include "nyla/commons/vec.h"

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

using FileHandle = void *;

enum class FileOpenMode
{
    Read = 1 << 0,
    Write = 1 << 1,
    Append = 1 << 2,
};
NYLA_BITENUM(FileOpenMode);

class NYLA_API Platform
{
  public:
    static constexpr inline uint64_t kPageAllocMinSize = 64_KiB;

    static auto GetMemPageSize() -> uint64_t;
    static auto ReserveMemPages(uint64_t size) -> char *;
    static void CommitMemPages(char *page, uint64_t size);
    static void DecommitMemPages(char *page, uint64_t size);

    static auto GetMonotonicTimeMillis() -> uint64_t;
    static auto GetMonotonicTimeMicros() -> uint64_t;
    static auto GetMonotonicTimeNanos() -> uint64_t;

    static auto Spawn(Span<const char *const> cmd) -> bool;

    static void Init(const PlatformInitDesc &desc);
    static void WinOpen();
    static auto WinGetSize() -> PlatformWindowSize;
    static auto WinPollEvent(PlatformEvent &outEvent) -> bool;

    static auto UpdateGamepad(uint32_t index) -> bool;
    static auto GetGamepadLeftStick(uint32_t index) -> float2;
    static auto GetGamepadRightStick(uint32_t index) -> float2;
    static auto GetGamepadLeftTrigger(uint32_t index) -> float;
    static auto GetGamepadRightTrigger(uint32_t index) -> float;

    static auto GetStdin() -> FileHandle;
    static auto GetStdout() -> FileHandle;
    static auto GetStderr() -> FileHandle;

    static auto FileValid(FileHandle file) -> bool;
    static auto FileOpen(const Path &path, FileOpenMode mode) -> FileHandle;
    static void FileClose(FileHandle file);
    static auto FileRead(FileHandle file, uint32_t size, char *out) -> uint32_t;
    static auto FileWrite(FileHandle file, uint32_t size, const char *in) -> uint32_t;
    static void FileSeek(FileHandle file, int64_t at);
    static auto FileTell(FileHandle file) -> uint64_t;

    static void ParseStdArgs(Str *args, uint32_t len);

    //

    template <typename T> static void FileWrite(FileHandle file, const T &data)
    {
        NYLA_ASSERT(FileWrite(file, sizeof(data), reinterpret_cast<const char *>(&data)) == sizeof(data));
    }

    template <typename T> static void FileWriteSpan(FileHandle file, Span<T> data)
    {
        uint64_t expectedSize = sizeof(data[0]) * data.Size();
        NYLA_ASSERT(FileWrite(file, expectedSize, reinterpret_cast<const char *>(&data[0])) == expectedSize);
    }
};

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
