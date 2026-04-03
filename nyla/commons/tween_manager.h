#pragma once

#include "nyla/commons/handle.h"

namespace nyla
{

struct Tween : Handle
{
};

class TweenManager
{
  public:
    static auto Now() -> float;
    static void Update(float dt);

    static auto BeginOf(Tween) -> float;
    static auto EndOf(Tween) -> float;

    static void Cancel(Tween);
    static auto Lerp(float &value, float endValue, float begin, float end) -> Tween;

  private:
};

} // namespace nyla