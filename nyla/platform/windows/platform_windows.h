#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/platform/platform.h"

#include "nyla/commons/containers/inline_ring.h"
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Xinput.h>

namespace nyla
{

class XInputGamePad;

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

    auto GetMemPageSize() -> uint64_t;
    auto ReserveMemPages(uint64_t size) -> char *;
    void CommitMemPages(char *page, uint64_t size);
    void DecommitMemPages(char *page, uint64_t size);

    auto GetMonotonicTimeMillis() -> uint64_t;
    auto GetMonotonicTimeMicros() -> uint64_t;
    auto GetMonotonicTimeNanos() -> uint64_t;

    auto UpdateGamepad(uint32_t index) -> bool;
    auto GetGamepadLeftStick(uint32_t index) -> float2;
    auto GetGamepadRightStick(uint32_t index) -> float2;
    auto GetGamepadLeftTrigger(uint32_t index) -> float;
    auto GetGamepadRightTrigger(uint32_t index) -> float;

  private:
    char *m_AddressSpaceBase;
    char *m_AddressSpaceAt;
    uint64_t m_AddressSpaceSize;

    SYSTEM_INFO m_SysInfo{};
    HINSTANCE m_HInstance{};
    HWND m_HWnd{};
    RECT m_WinRect{};

    std::array<XINPUT_STATE, 1> m_Gamepads{};

    static inline constexpr uint32_t kFlagQuit = 1 << 0;
    static inline constexpr uint32_t kFlagWinResize = 1 << 1;
    static inline constexpr uint32_t kFlagRepaint = 1 << 2;
    uint32_t m_Flags{};

    InlineRing<PlatformEvent, 32> m_EventsRing{};
};

} // namespace nyla