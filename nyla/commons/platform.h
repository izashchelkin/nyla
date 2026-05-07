#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

void API ParseStdArgs(byteview *args, uint32_t maxArgs);

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
    MouseMove,
    TextInput,

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
            uint32_t code; // button id for Press/Release; 0 for Move
            int32_t x;     // window-relative pixels
            int32_t y;
        } mouse;
    };
    // utf8 bytes produced by typing; populated on KeyDown (Linux xkb) or TextInput (Windows WM_CHAR).
    // Control codes follow xkb/WM_CHAR conventions: 0x08 Backspace, 0x09 Tab, 0x0D Enter, 0x1B Escape.
    uint8_t textBytes[4];
    uint8_t textLen;
};

auto API GenRandom64() -> uint64_t;
void API Sleep(uint64_t millis);
auto API Spawn(span<const char *const> cmd) -> bool;
auto API RunSync(span<const char *const> cmd, region_alloc &alloc, byteview &outLog) -> int32_t;
void API WinOpen();
void API WinSetTitle(byteview title);
auto API WinGetSize() -> PlatformWindowSize;
auto API WinPollEvent(PlatformEvent &outEvent) -> bool;

} // namespace nyla