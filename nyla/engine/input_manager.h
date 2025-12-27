#pragma once

#include "nyla/commons/containers/inline_vec.h"

#include <cstdint>
#include <limits>

namespace nyla
{

namespace
{

} // namespace

struct InputId
{
    uint8_t index;
};

class InputManager
{
  public:
    auto NewId() -> InputId;
    void Map(InputId input, uint32_t type, uint32_t code);
    void HandlePressed(uint32_t type, uint32_t code, uint64_t time);
    void HandleReleased(uint32_t type, uint32_t code, uint64_t time);
    auto IsPressed(InputId input) -> bool;

    void Update();

  private:
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
};

extern InputManager *g_InputManager;

} // namespace nyla