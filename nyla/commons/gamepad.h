#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/vec.h"

namespace nyla
{

auto API UpdateGamepad(uint32_t index) -> bool;
auto API GetGamepadLeftStick(uint32_t index) -> float2;
auto API GetGamepadRightStick(uint32_t index) -> float2;
auto API GetGamepadLeftTrigger(uint32_t index) -> float;
auto API GetGamepadRightTrigger(uint32_t index) -> float;

} // namespace nyla