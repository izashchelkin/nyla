#pragma once

#include <cstdint>
#include <string>

#include "nyla/platform/abstract_input.h"
#include "nyla/platform/key_physical.h"

namespace nyla
{

struct PlatformInitDesc
{
    bool keyboardInput;
    bool mouseInput;
};
void PlatformInit(PlatformInitDesc);

void PlatformMapInputBegin();
void PlatformMapInput(AbstractInputMapping mapping, KeyPhysical key);
void PlatformMapInputEnd();

struct PlatformWindow
{
    uint32_t handle;
};
auto PlatformCreateWindow() -> PlatformWindow;

struct PlatformWindowSize
{
    uint32_t width;
    uint32_t height;
};

auto PlatformGetWindowSize(PlatformWindow window) -> PlatformWindowSize;

struct PlatformFileChanged
{
    bool seen;
    std::string path;
};
void PlatformFsWatchFile(const std::string &path);
auto PlatformFsGetFileChanges() -> std::span<PlatformFileChanged>;

struct PlatformProcessEventsResult
{
    bool shouldRedraw;
};
auto PlatformProcessEvents() -> PlatformProcessEventsResult;

auto PlatformShouldExit() -> bool;

} // namespace nyla