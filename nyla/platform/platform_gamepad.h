#pragma once

#include "nyla/commons/math/vec.h"
namespace nyla
{

class GamePad
{
  public:
    virtual auto GetLeftStick() -> float2 = 0;
    virtual auto GetRightStick() -> float2 = 0;
};

} // namespace nyla