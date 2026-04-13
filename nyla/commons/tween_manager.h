#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"

namespace nyla
{

struct tween : handle
{
};

namespace TweenManager
{

auto API Now() -> float;
void API Update(float dt);

auto API BeginOf(tween) -> float;
auto API EndOf(tween) -> float;

void API Cancel(tween);
auto API Lerp(float &value, float endValue, float begin, float end) -> tween;

} // namespace TweenManager

} // namespace nyla