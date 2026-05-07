#pragma once

#include <cstdint>

#include "nyla/commons/keyboard.h"
#include "nyla/commons/macros.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Xinput.h>
#include <bcrypt.h>

namespace nyla
{

namespace win32
{

auto API GetHInstance() -> HINSTANCE;
void API SetHInstance(HINSTANCE hInstance);

auto API WinGetHandle() -> HWND;
auto API ScanCodeToKeyPhysical(uint8_t scanCode, bool extended) -> KeyPhysical;

} // namespace win32

} // namespace nyla