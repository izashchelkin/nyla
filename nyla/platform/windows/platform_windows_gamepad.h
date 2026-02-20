#pragma once

#include "nyla/commons/math/vec.h"
#include "nyla/platform/platform_gamepad.h"
#include "nyla/platform/windows/platform_windows.h"

namespace nyla
{

class XInputGamePad final : public GamePad
{
  public:
    auto GetLeftStick() -> float2 final;
    auto GetRightStick() -> float2 final;

    auto SetGamePad(XINPUT_GAMEPAD gamepad)
    {
        m_Gamepad = gamepad;
    }

  private:
    XINPUT_GAMEPAD m_Gamepad;
};

} // namespace nyla
