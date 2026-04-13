#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

void ParseStdArgs(byteview *args, uint32_t maxArgs);

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

auto API Sleep(uint64_t millis);
auto API Spawn(const char *const cmd, uint64_t count) -> bool;
void API WinOpen();
auto API WinGetSize() -> PlatformWindowSize;
auto API WinPollEvent(PlatformEvent &outEvent) -> bool;

} // namespace nyla