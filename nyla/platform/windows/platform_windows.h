#pragma once

#include "nyla/platform/platform.h"

#include "nyla/commons/containers/inline_ring.h"
#include <cstdint>

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

    auto WinGetHandle() -> HWND;
    void WinOpen();
    auto WinGetSize() -> PlatformWindowSize;

    auto PollEvent(PlatformEvent &outEvent) -> bool;

    auto MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

    auto PageAlloc(uint32_t &inOutSize, void *&outBase) -> bool;

    auto GetMonotonicTimeMillis() -> uint64_t;
    auto GetMonotonicTimeMicros() -> uint64_t;
    auto GetMonotonicTimeNanos() -> uint64_t;

  private:
    HINSTANCE m_HInstance{};
    HWND m_HWnd{};
    RECT m_WinRect{};

    static inline constexpr uint32_t kFlagQuit = 1 << 0;
    static inline constexpr uint32_t kFlagWinResize = 1 << 1;
    static inline constexpr uint32_t kFlagRepaint = 1 << 2;
    uint32_t m_Flags{};

    InlineRing<PlatformEvent, 32> m_EventsRing{};
};

} // namespace nyla