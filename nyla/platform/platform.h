#pragma once

#include "nyla/commons/bitenum.h"
#include <cstdint>

namespace nyla
{

enum class PlatformFeature
{
    KeyboardInput,
    MouseInput,
};
NYLA_BITENUM(PlatformFeature);

struct PlatformWindow
{
    uint32_t handle;
};

struct PlatformWindowSize
{
    uint32_t width;
    uint32_t height;
};

enum class PlatformEventType
{
    None,

    KeyPress,
    KeyRelease,
    MousePress,
    MouseRelease,

    ShouldRedraw,
    ShouldExit
};

struct PlatformEvent
{
    PlatformEventType type;
    union {
        struct
        {
            uint32_t code;
        } key;

        struct
        {
            uint32_t code;
        } mouse;
    };
};

struct PlatformInitDesc
{
    PlatformFeature enabledFeatures;
};

class Platform
{
  public:
    void Init(const PlatformInitDesc &desc);
    auto CreateWindow() -> PlatformWindow;
    auto GetWindowSize(PlatformWindow window) -> PlatformWindowSize;
    auto PollEvent(PlatformEvent &outEvent) -> bool;

  private:
    class Impl;
    Impl *m_Impl;
};
extern Platform *g_Platform;

} // namespace nyla