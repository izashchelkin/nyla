#pragma once

#include "nyla/platform/platform.h"

#include "nyla/commons/containers/inline_ring.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace nyla
{

class Platform::Impl
{
  public:
    void Init(const PlatformInitDesc &desc);
    auto GetHInstance() -> HINSTANCE;
    void SetHInstance(HINSTANCE hInstance);

    auto CreateWin() -> HWND;
    auto GetWindowSize(HWND window) -> PlatformWindowSize;

    auto PollEvent(PlatformEvent &outEvent) -> bool;
    void EnqueueEvent(const PlatformEvent &);

  private:
    InlineRing<PlatformEvent, 8> m_EventsRing{};
    HINSTANCE m_HInstance{};
};

} // namespace nyla