#include <limits>

#include "nyla/commons/math/vec.h"
#include "nyla/platform/windows/platform_windows.h"
#include "nyla/platform/windows/platform_windows_gamepad.h"

namespace nyla
{

namespace
{

auto GetStickInternal(auto rawx, auto rawy, auto rawdeadzone) -> float2
{
    const float x = (float)rawx / (float)std::numeric_limits<uint16_t>::max();
    const float y = (float)rawy / (float)std::numeric_limits<uint16_t>::max();

    const float magnitude = std::sqrt(x * x + y * y);
    const float deadzone = (float)rawdeadzone / (float)std::numeric_limits<uint16_t>::max();

    if (magnitude < deadzone)
    {
        return {0.0f, 0.0f};
    }

    // Renormalize so movement starts smoothly from the edge of the deadzone
    float scale = (magnitude - deadzone) / (1.0f - deadzone);

    return {(x / magnitude) * scale, (y / magnitude) * scale};
}

} // namespace

auto XInputGamePad::GetLeftStick() -> float2
{
    return GetStickInternal(m_Gamepad.sThumbLX, m_Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

auto XInputGamePad::GetRightStick() -> float2
{
    return GetStickInternal(m_Gamepad.sThumbRX, m_Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
}

} // namespace nyla
