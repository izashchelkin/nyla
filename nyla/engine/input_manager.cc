#include "nyla/engine/input_manager.h"
#include "nyla/commons/containers/inline_vec.h"

#include <cstdint>

namespace nyla
{

auto InputManager::NewId() -> InputId
{
    m_InputStates.emplace_back(InputState{});
    return {.index = static_cast<uint8_t>(m_InputStates.size() - 1)};
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
            state.pressedAt = std::max(state.pressedAt, time);
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

InputManager *g_InputManager = new InputManager{};

} // namespace nyla