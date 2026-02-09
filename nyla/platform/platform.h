#pragma once

#include "nyla/commons/assert.h"
#include "nyla/commons/bitenum.h"
#include "nyla/commons/byteliterals.h"
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
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

    void Init(const PlatformInitDesc &desc);
    void WinOpen();
    auto WinGetSize() -> PlatformWindowSize;
    auto PollEvent(PlatformEvent &outEvent) -> bool;

    auto GetMemPageSize() -> uint64_t;
    auto ReserveMemPages(uint64_t size) -> char *;
    void CommitMemPages(char *page, uint64_t size);
    void DecommitMemPages(char *page, uint64_t size);

    auto GetMonotonicTimeMillis() -> uint64_t;
    auto GetMonotonicTimeMicros() -> uint64_t;
    auto GetMonotonicTimeNanos() -> uint64_t;

    auto Spawn(std::span<const char *const> cmd) -> bool;

    class Impl;

    void SetImpl(Impl *impl)
    {
        m_Impl = impl;
    }

    auto GetImpl() -> auto *
    {
        return m_Impl;
    }

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

    auto ReadFile(const std::string &filename) -> std::vector<std::byte>
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        return ReadFileInternal(file);
    }

    //

  private:
    Impl *m_Impl;
};
extern Platform g_Platform;

auto PlatformMain() -> int;

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