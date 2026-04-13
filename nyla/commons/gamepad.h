#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

auto API UpdateGamepad(uint32_t index) -> bool;
void API GetGamepadLeftStick(uint32_t index, float &outX, float &outY);
void API GetGamepadRightStick(uint32_t index, float &outX, float &outY);
auto API GetGamepadLeftTrigger(uint32_t index) -> float;
auto API GetGamepadRightTrigger(uint32_t index) -> float;

} // namespace nyla