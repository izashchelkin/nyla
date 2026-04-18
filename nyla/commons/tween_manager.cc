#include "nyla/commons/tween_manager.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/region_alloc.h"

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

struct tween_manager
{
    float ts;
    handle_pool<tween, tween_data, 1024> tweens;
};
tween_manager *manager;

} // namespace

namespace TweenManager
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<tween_manager>(RegionAlloc::g_BootstrapAlloc);
}

auto API Now() -> float
{
    return manager->ts;
}

void API Update(float dt)
{
    manager->ts += dt;

    for (auto &slot : manager->tweens)
    {
        if (!slot.used)
            continue;

        tween_data &tweenData = slot.data;
        ASSERT(tweenData.end > tweenData.begin);

        if (manager->ts >= tweenData.end)
        {
            *tweenData.value = tweenData.endValue;
            HandlePool::Free(slot);
            continue;
        }

        if (manager->ts >= tweenData.begin)
        {
            float duration = tweenData.end - tweenData.begin;
            float passed = manager->ts - tweenData.begin;
            float t = passed / duration;

            *tweenData.value = tweenData.endValue * t + tweenData.startValue * (1.f - t);
        }
    }
}

void API Cancel(tween tween)
{
    if (auto [ok, slotPtr] = HandlePool::TryResolveSlot(manager->tweens, tween); ok)
        HandlePool::Free(*slotPtr);
}

auto API Lerp(float &value, float endValue, float begin, float end) -> tween
{
    ASSERT(end > begin);
    return HandlePool::Acquire(manager->tweens, tween_data{
                                                    .value = &value,
                                                    .begin = begin,
                                                    .end = end,
                                                    .startValue = value,
                                                    .endValue = endValue,
                                                });
}

auto API BeginOf(tween tween) -> float
{
    if (auto [ok, slotPtr] = HandlePool::TryResolveSlot(manager->tweens, tween); ok)
        return slotPtr->data.begin;
    else
        return 0.f;
}

auto API EndOf(tween tween) -> float
{
    if (auto [ok, slotPtr] = HandlePool::TryResolveSlot(manager->tweens, tween); ok)
        return slotPtr->data.begin;
    else
        return 0.f;
}

} // namespace TweenManager

} // namespace nyla