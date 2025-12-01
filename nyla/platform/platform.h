#pragma once

#include <cstdint>
#include <string>

#include "nyla/platform/abstract_input.h"
#include "nyla/platform/key_physical.h"

namespace nyla {

void PlatformInit();

void PlatformMapInputBegin();
void PlatformMapInput(AbstractInputMapping mapping, KeyPhysical key);
void PlatformMapInputEnd();

struct PlatformWindow {
  uint32_t handle;
};
PlatformWindow PlatformCreateWindow();

struct PlatformWindowSize {
  uint32_t width;
  uint32_t height;
};

PlatformWindowSize PlatformGetWindowSize(PlatformWindow window);

void PlatformFsWatch(const std::string& path);

struct PlatformFsChange {
  bool isdir;
  bool seen;
  std::string path;
};
std::span<PlatformFsChange> PlatformFsGetChanges();

void PlatformProcessEvents();
bool PlatformShouldExit();

}  // namespace nyla