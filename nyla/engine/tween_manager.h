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
    auto Now() -> float
    {
        return m_now;
    }

    void Update(float dt);

    auto BeginOf(Tween) -> float;
    auto EndOf(Tween) -> float;

    void Cancel(Tween);
    auto Lerp(float &value, float endValue, float begin, float end) -> Tween;

  private:
    struct TweenData
    {
        float *value;
        float begin;
        float end;
        float startValue;
        float endValue;
    };
    HandlePool<Tween, TweenData, 1024> m_tweens;

    float m_now;
};

extern TweenManager *g_TweenManager;

} // namespace nyla