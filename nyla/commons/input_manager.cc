#include <cstdint>
#include <limits>

#include "nyla/commons/array.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/minmax.h"

namespace nyla
{

namespace
{

struct InputState
{
    uint64_t pressedAt;
    bool released;

    struct Physical
    {
        uint32_t type;
        uint32_t code;
    };
    Physical mappedTo;
};
InlineVec<InputState, std::numeric_limits<uint8_t>::max() + 1> m_InputStates;

} // namespace

auto InputManager::NewId() -> InputId
{
    m_InputStates.PushBack(InputState{});
    return {.index = static_cast<uint8_t>(m_InputStates.Size() - 1)};
}

auto InputManager::NewIdMapped(uint32_t type, uint32_t code) -> InputId
{
    auto ret = NewId();
    Map(ret, type, code);
    return ret;
}

void InputManager::Map(InputId input, uint32_t type, uint32_t code)
{
    m_InputStates[input.index].mappedTo = {
        .type = type,
        .code = code,
    };
}

void InputManager::HandlePressed(uint32_t type, uint32_t code, uint64_t time)
{
    for (auto &state : m_InputStates)
    {
        if (state.mappedTo.type == type && state.mappedTo.code == code)
        {
            state.pressedAt = Max(state.pressedAt, time);
            break;
        }
    }
}

void InputManager::HandleReleased(uint32_t type, uint32_t code, uint64_t time)
{
    for (auto &state : m_InputStates)
    {
        if (state.mappedTo.type == type && state.mappedTo.code == code)
        {
            state.released = true;
            break;
        }
    }
}

auto InputManager::IsPressed(InputId input) -> bool
{
    auto &state = m_InputStates[input.index];
    return !state.released && state.pressedAt;
}

void InputManager::Update()
{
    for (auto &state : m_InputStates)
    {
        if (!state.released)
            continue;

        state.pressedAt = 0;
        state.released = false;
    }
}

} // namespace nyla