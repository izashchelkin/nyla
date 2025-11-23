#pragma once

#include <cstdint>

#include "nyla/platform/abstract_input.h"
#include "nyla/platform/key_physical.h"

namespace nyla {

void PlatformInit();

void PlatformMapInputBegin();
void PlatformMapInput(AbstractInputMapping mapping, KeyPhysical key);
void PlatformMapInputEnd();

using PlatformWindow = uint32_t;
PlatformWindow PlatformCreateWindow();

struct PlatformWindowSize {
  uint32_t width;
  uint32_t height;
};

PlatformWindowSize PlatformGetWindowSize(PlatformWindow window);

void PlatformProcessEvents();
bool PlatformShouldExit();

}  // namespace nyla