#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"

namespace nyla
{

struct Tween : Handle
{
};

class TweenManager
{
  public:
    void Update(float dt);

    auto CurrentTime() -> float
    {
        return m_time;
    }

    auto BeginOf(Tween tween) -> float;
    auto EndOf(Tween tween) -> float;

    auto Lerp(float *value, float endValue, float begin, float end) -> Tween;

  private:
    struct TweenData
    {
        float *value;
        float begin;
        float end;
        float startValue;
        float endValue;
    };
    HandlePool<Tween, TweenData, 64> m_tweens;

    float m_time;
};

} // namespace nyla