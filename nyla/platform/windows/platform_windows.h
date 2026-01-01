#pragma once

#include "nyla/platform/platform.h"

#include "nyla/commons/containers/inline_ring.h"

#ifndef _WINDEF_
typedef struct HINSTANCE__ *HINSTANCE;
typedef struct HWND__ *HWND;
#endif

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