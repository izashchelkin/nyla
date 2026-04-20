#include "nyla/commons/input_manager.h"

#include <cstdint>

#include "nyla/commons/array.h"      // IWYU pragma: keep
#include "nyla/commons/inline_vec.h" // IWYU pragma: keep
#include "nyla/commons/minmax.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

namespace
{

struct input_state
{
    uint64_t pressedAt;
    uint32_t code;
    input_interface_type type;
    bool released;
};

struct input_manager
{
    inline_vec<input_state, 0xFF> inputStates;
};
input_manager *manager;

//

auto FindInput(input_interface_type type, uint32_t code) -> input_state *
{
    for (auto it = manager->inputStates.begin(), end = manager->inputStates.end(); it != end; ++it)
    {
        if (it->type == type && it->code == code)
            return it;
    }

    return nullptr;
}

} // namespace

namespace InputManager
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<input_manager>(RegionAlloc::g_BootstrapAlloc);
}

void API Map(input_id input, input_interface_type type, uint32_t code)
{
    auto &state = manager->inputStates[(uint8_t)input];
    state.type = type;
    state.code = code;
}

void API HandlePressed(input_interface_type type, uint32_t code, uint64_t time)
{
    if (auto state = FindInput(type, code); state)
        state->pressedAt = Max(state->pressedAt, time);
}

void API HandleReleased(input_interface_type type, uint32_t code, uint64_t time)
{
    if (auto state = FindInput(type, code); state)
        state->released = true;
}

auto API IsPressed(input_id input) -> bool
{
    auto &state = manager->inputStates[(uint8_t)input];
    return !state.released && state.pressedAt;
}

void API Update()
{
    for (auto &state : manager->inputStates)
    {
        if (state.released)
        {
            state.pressedAt = 0;
            state.released = false;
        }
    }
}

} // namespace InputManager

} // namespace nyla