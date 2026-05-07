#pragma once

#include <cstdint>

#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct engine_init_desc
{
    uint32_t maxFps;
    bool vsync;
};

// Bit flags for key_event::mods. Tracked from physical Shift/Ctrl/Alt KeyDown/Up edges.
namespace KeyMod
{
constexpr uint8_t Shift = 1u << 0;
constexpr uint8_t Ctrl = 1u << 1;
constexpr uint8_t Alt = 1u << 2;
} // namespace KeyMod

struct key_event
{
    KeyPhysical key;
    uint8_t mods; // KeyMod bitmask at the moment of KeyDown
};

struct engine_frame
{
    rhi_cmdlist cmd;
    float dt;
    uint64_t dtUs;
    uint64_t frameStartUs;
    uint32_t fps;
    byteview textChars;            // utf8 bytes typed this frame; valid for the frame only
    span<const key_event> keyDown; // KeyDown edges this frame (incl. autorepeat); valid for the frame only

    // Pointer (mouse) state. Coordinates are window-relative pixels.
    // pointerButtons: current state bitmask, bit (button-1) set while held (X11 button ids; 1=Left, 2=Middle, 3=Right).
    // pointerPress / pointerRelease: edges this frame, same bit layout, cleared each FrameBegin.
    // pointerDx / pointerDy: pixel delta accumulated this frame.
    int32_t pointerX;
    int32_t pointerY;
    int32_t pointerDx;
    int32_t pointerDy;
    uint32_t pointerButtons;
    uint32_t pointerPress;
    uint32_t pointerRelease;
};

namespace Engine
{

void API Bootstrap(region_alloc &alloc, const engine_init_desc &desc);
auto API FrameBegin(region_alloc &alloc) -> engine_frame;
void API FrameEnd(region_alloc &alloc);
auto API IsWindowResized() -> bool;
auto API ShouldExit() -> bool;
void API RequestExit();

} // namespace Engine

} // namespace nyla