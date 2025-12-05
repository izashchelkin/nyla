#include "nyla/platform/abstract_input.h"

#include <cstdint>

namespace nyla
{

template <typename H> auto AbslHashValue(H h, const AbstractInputId &id) -> H
{
    return H::combine(std::move(h), id.tag, id.code);
}

auto operator==(const AbstractInputId &lhs, const AbstractInputId &rhs) -> bool
{
    return lhs.tag == rhs.tag && lhs.code == rhs.code;
}

template <typename H> auto AbslHashValue(H h, const AbstractInputMapping &id) -> H
{
    return H::combine(std::move(h), id.val);
}

auto operator==(const AbstractInputMapping &lhs, const AbstractInputMapping &rhs) -> bool
{
    return lhs.val == rhs.val;
}

namespace
{

struct InputState
{
    uint64_t pressed_at;
    bool released;
};

} // namespace

static Map<AbstractInputId, InputState> inputstate;
static Map<AbstractInputMapping, AbstractInputId> inputmapping;

void AbstractInputHandlePressed(AbstractInputId id, uint64_t ts)
{
    auto [it, ok] = inputstate.try_emplace(id, InputState{.pressed_at = ts});
    if (ok)
        return;

    auto &[_, state] = *it;
    if (state.pressed_at)
        return;

    state.pressed_at = ts;
}

void AbstractInputHandleReleased(AbstractInputId id)
{
    auto [it, ok] = inputstate.try_emplace(id, InputState{.released = true});
    if (ok)
        return;

    auto &[_, state] = *it;
    state.released = true;
}

void AbstractInputRelease(AbstractInputMapping mapping)
{
    auto id = inputmapping.at(mapping);
    auto it = inputstate.find(id);
    if (it == inputstate.end())
    {
        return;
    }

    auto &[_, state] = *it;
    state.released = true;
}

void AbstractInputProcessFrame()
{
    for (auto &[_, state] : inputstate)
    {
        if (state.released)
        {
            state.pressed_at = 0;
            state.released = false;
        }
    }
}

void AbstractInputMapId(AbstractInputMapping mapping, AbstractInputId id)
{
    inputmapping.emplace(mapping, id);
}

auto Pressed(AbstractInputMapping mapping) -> bool
{
    auto id = inputmapping.at(mapping);
    auto it = inputstate.find(id);
    if (it == inputstate.end())
    {
        return false;
    }

    auto &[_, state] = *it;
    return state.pressed_at > 0;
}

auto PressedFor(AbstractInputMapping mapping, uint64_t now) -> uint32_t
{
    auto id = inputmapping.at(mapping);
    auto it = inputstate.find(id);
    if (it == inputstate.end())
    {
        return 0;
    }

    auto &[_, state] = *it;
    if (!state.pressed_at)
    {
        return 0;
    }

    return now - state.pressed_at + 1;
}

} // namespace nyla