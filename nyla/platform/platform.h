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

class PlatformImpl
{
    void *data;
    void (*init)(void *data, const PlatformInitDesc &);
    PlatformWindow (*createWindow)(void *data);
    PlatformWindowSize (*getWindowSize)(void *data, PlatformWindow);
    bool (*pollEvent)(void *data, PlatformEvent &outEvent);
};
extern PlatformImpl *g_PlatformImpl;

class Platform
{
  public:
    Platform(PlatformImpl *impl) : m_Impl{impl} {};

    void Init(const PlatformInitDesc &desc)
    {
        m_Impl->init(m_Impl->data, desc);
    }

    auto CreateWindow()
    {
        return m_Impl->createWindow(m_Impl->data);
    }

    auto GetWindowSize(PlatformWindow window)
    {
        return m_Impl->getWindowSize(m_Impl->data, window);
    }

    auto PollEvent(PlatformEvent &outEvent)
    {
        return m_Impl->pollEvent(m_Impl->data, outEvent);
    }

  private:
    PlatformImpl *m_Impl;
};
extern Platform *g_Platform;

} // namespace nyla