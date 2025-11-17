#pragma once

#include <cstdint>

#include "nyla/commons/containers/map.h"

namespace nyla {

struct InputId {
  uint32_t tag;
  uint32_t code;
};

struct InputMappingId {
  int val;

  InputMappingId() {
    static int next = 0;
    val = next++;
  }
};

void InputHandleFrame();
void InputHandlePressed(InputId id, uint64_t ts);
void InputHandleReleased(InputId id);
void InputRelease(InputMappingId mapping);

void InputMapId(InputMappingId mapping, InputId id);
bool Pressed(InputMappingId mapping);
uint32_t PressedFor(InputMappingId mapping, uint64_t now);

}  // namespace nyla