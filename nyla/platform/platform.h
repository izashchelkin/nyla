#pragma once

#include "nyla/commons/assert.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/commons/vec.h"
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    bool open;
};

class Platform
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

    static auto Spawn(std::span<const char *const> cmd) -> bool;

    static void InitGraphical(const PlatformInitDesc &desc);
    static void WinOpen();
    static auto WinGetSize() -> PlatformWindowSize;
    static auto WinPollEvent(PlatformEvent &outEvent) -> bool;

    static auto UpdateGamepad(uint32_t index) -> bool;
    static auto GetGamepadLeftStick(uint32_t index) -> float2;
    static auto GetGamepadRightStick(uint32_t index) -> float2;
    static auto GetGamepadLeftTrigger(uint32_t index) -> float;
    static auto GetGamepadRightTrigger(uint32_t index) -> float;

    // TODO: move this

    static auto ReadFileInternal(std::ifstream &file) -> std::vector<std::byte>
    {
        NYLA_ASSERT(file.is_open());

        std::vector<std::byte> buffer(file.tellg());

        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), static_cast<long>(buffer.size()));

        file.close();
        return buffer;
    }

    static auto ReadFile(std::string_view filename) -> std::vector<std::byte>
    {
        std::ifstream file(std::string{filename}, std::ios::ate | std::ios::binary);
        return ReadFileInternal(file);
    }

    static auto ReadFile(const std::string &filename) -> std::vector<std::byte>
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        return ReadFileInternal(file);
    }
};

auto PlatformMain(std::span<const char *> argv) -> int;

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