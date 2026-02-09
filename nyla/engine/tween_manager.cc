#include "nyla/engine/tween_manager.h"
#include "nyla/commons/handle_pool.h"

namespace nyla
{

void TweenManager::Update(float dt)
{
    m_Now += dt;

    for (auto &slot : m_Tweens)
    {
        if (!slot.used)
            continue;

        TweenData &tweenData = slot.data;
        NYLA_ASSERT(tweenData.end > tweenData.begin);

        if (m_Now >= tweenData.end)
        {
            *tweenData.value = tweenData.endValue;
            slot.Free();
            continue;
        }

        if (m_Now >= tweenData.begin)
        {
            float duration = tweenData.end - tweenData.begin;
            float passed = m_Now - tweenData.begin;
            float t = passed / duration;

            *tweenData.value = tweenData.endValue * t + tweenData.startValue * (1.f - t);
        }
    }
}

void TweenManager::Cancel(Tween tween)
{
    if (auto [ok, slot] = m_Tweens.TryResolveSlot(tween); ok)
        slot->Free();
}

auto TweenManager::Lerp(float &value, float endValue, float begin, float end) -> Tween
{
    NYLA_ASSERT(end > begin);
    return m_Tweens.Acquire(TweenData{
        .value = &value,
        .begin = begin,
        .end = end,
        .startValue = value,
        .endValue = endValue,
    });
}

auto TweenManager::BeginOf(Tween tween) -> float
{
    if (auto [ok, slot] = m_Tweens.TryResolveSlot(tween); ok)
        return slot->data.begin;
    else
        return 0.f;
}

auto TweenManager::EndOf(Tween tween) -> float
{
    if (auto [ok, slot] = m_Tweens.TryResolveSlot(tween); ok)
        return slot->data.begin;
    else
        return 0.f;
}

} // namespace nyla