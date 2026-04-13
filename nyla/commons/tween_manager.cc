#include "nyla/commons/tween_manager.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/handle_pool.h"

namespace nyla
{

namespace
{

struct tween_data
{
    float *value;
    float begin;
    float end;
    float startValue;
    float endValue;
};

struct tweenmanager_state
{
    float ts;
    handle_pool<tween, tween_data, 1024> tweens;
};
tweenmanager_state *g_State; // TODO: init this guy!

} // namespace

auto TweenManager::Now() -> float
{
    return g_State->ts;
}

void TweenManager::Update(float dt)
{
    g_State->ts += dt;

    for (auto &slot : g_State->tweens)
    {
        if (!slot.used)
            continue;

        tween_data &tweenData = slot.data;
        ASSERT(tweenData.end > tweenData.begin);

        if (g_State->ts >= tweenData.end)
        {
            *tweenData.value = tweenData.endValue;
            HandlePool::Free(slot);
            continue;
        }

        if (g_State->ts >= tweenData.begin)
        {
            float duration = tweenData.end - tweenData.begin;
            float passed = g_State->ts - tweenData.begin;
            float t = passed / duration;

            *tweenData.value = tweenData.endValue * t + tweenData.startValue * (1.f - t);
        }
    }
}

void TweenManager::Cancel(tween tween)
{
    if (auto [ok, slotPtr] = HandlePool::TryResolveSlot(g_State->tweens, tween); ok)
        HandlePool::Free(*slotPtr);
}

auto TweenManager::Lerp(float &value, float endValue, float begin, float end) -> tween
{
    ASSERT(end > begin);
    return HandlePool::Acquire(g_State->tweens, tween_data{
                                                    .value = &value,
                                                    .begin = begin,
                                                    .end = end,
                                                    .startValue = value,
                                                    .endValue = endValue,
                                                });
}

auto TweenManager::BeginOf(tween tween) -> float
{
    if (auto [ok, slotPtr] = HandlePool::TryResolveSlot(g_State->tweens, tween); ok)
        return slotPtr->data.begin;
    else
        return 0.f;
}

auto TweenManager::EndOf(tween tween) -> float
{
    if (auto [ok, slotPtr] = HandlePool::TryResolveSlot(g_State->tweens, tween); ok)
        return slotPtr->data.begin;
    else
        return 0.f;
}

} // namespace nyla