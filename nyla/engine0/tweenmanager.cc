#include "nyla/engine0/tweenmanager.h"
#include "absl/log/log.h"
#include "nyla/commons/handle_pool.h"

namespace nyla
{

void TweenManager::Update(float dt)
{
    m_time += dt;

    for (auto &slot : m_tweens)
    {
        if (!slot.used)
            continue;

        TweenData &tweenData = slot.data;
        CHECK_GT(tweenData.end, tweenData.begin);

        if (m_time >= tweenData.end)
        {
            *tweenData.value = tweenData.endValue;
            slot.used = false;
            continue;
        }

        if (m_time >= tweenData.begin)
        {
            float duration = tweenData.end - tweenData.begin;
            float passed = m_time - tweenData.begin;
            float t = passed / duration;

            LOG(INFO) << t;

            *tweenData.value = tweenData.endValue * t + tweenData.startValue * (1.f - t);
        }
    }
}

auto TweenManager::Lerp(float *value, float endValue, float begin, float end) -> Tween
{
    CHECK_GT(end, begin);
    return m_tweens.Acquire(TweenData{
        .value = value,
        .begin = begin,
        .end = end,
        .startValue = *value,
        .endValue = endValue,
    });
}

auto TweenManager::BeginOf(Tween tween) -> float
{
    if (auto [ok, slot] = m_tweens.TryResolveSlot(tween); ok)
        return slot->data.begin;
    else
        return 0.f;
}

auto TweenManager::EndOf(Tween tween) -> float
{
    if (auto [ok, slot] = m_tweens.TryResolveSlot(tween); ok)
        return slot->data.begin;
    else
        return 0.f;
}

} // namespace nyla