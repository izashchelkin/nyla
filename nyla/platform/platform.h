#pragma once

#include "nyla/commons/bitenum.h"
#include <cstdint>

namespace nyla
{

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
    auto CreateWin() -> PlatformWindow;
    auto GetWindowSize(PlatformWindow window) -> PlatformWindowSize;
    auto PollEvent(PlatformEvent &outEvent) -> bool;

    class Impl;

    void SetImpl(Impl *impl)
    {
        m_Impl = impl;
    }

    auto GetImpl() -> auto *
    {
        return m_Impl;
    }

  private:
    Impl *m_Impl;
};
extern Platform *g_Platform;

int PlatformMain();

} // namespace nyla