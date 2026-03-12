#pragma once

#include <cstdint>

namespace nyla
{

struct InputId
{
    uint8_t index;
};

class InputManager
{
  public:
    static auto NewId() -> InputId;
    static auto NewIdMapped(uint32_t type, uint32_t code) -> InputId;
    static void Map(InputId input, uint32_t type, uint32_t code);
    static void HandlePressed(uint32_t type, uint32_t code, uint64_t time);
    static void HandleReleased(uint32_t type, uint32_t code, uint64_t time);
    static auto IsPressed(InputId input) -> bool;

    static void Update();
};

} // namespace nyla