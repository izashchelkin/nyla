#pragma once

#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Xinput.h>
#include <bcrypt.h>

#include "nyla/commons/keyboard.h"

namespace nyla
{

class WindowsPlatform
{
  public:
    static auto MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

    static auto GetHInstance() -> HINSTANCE;
    static void SetHInstance(HINSTANCE hInstance);

    static auto WinGetHandle() -> HWND;

    static auto ScanCodeToKeyPhysical(uint8_t scanCode, bool extended) -> KeyPhysical;
};

} // namespace nyla