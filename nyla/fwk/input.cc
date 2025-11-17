#include "nyla/fwk/input.h"

#include <cstdint>

namespace nyla {

template <typename H>
H AbslHashValue(H h, const InputId& id) {
  return H::combine(std::move(h), id.tag, id.code);
}

bool operator==(const InputId& lhs, const InputId& rhs) {
  return lhs.tag == rhs.tag && lhs.code == rhs.code;
}

template <typename H>
H AbslHashValue(H h, const InputMappingId& id) {
  return H::combine(std::move(h), id.val);
}

bool operator==(const InputMappingId& lhs, const InputMappingId& rhs) {
  return lhs.val == rhs.val;
}

namespace {

struct InputState {
  uint64_t pressed_at;
  bool released;
};

}  // namespace

static Map<InputId, InputState> inputstate;
static Map<InputMappingId, InputId> inputmapping;

void InputHandlePressed(InputId id, uint64_t ts) {
  auto [it, ok] = inputstate.try_emplace(id, InputState{.pressed_at = ts});
  if (ok) return;

  auto& [_, state] = *it;
  if (state.pressed_at) return;

  state.pressed_at = ts;
}

void InputHandleReleased(InputId id) {
  auto [it, ok] = inputstate.try_emplace(id, InputState{.released = true});
  if (ok) return;

  auto& [_, state] = *it;
  state.released = true;
}

void InputRelease(InputMappingId mapping) {
  auto id = inputmapping.at(mapping);
  auto it = inputstate.find(id);
  if (it == inputstate.end()) {
    return;
  }

  auto& [_, state] = *it;
  state.released = true;
}

void InputHandleFrame() {
  for (auto& [_, state] : inputstate) {
    if (state.released) {
      state.pressed_at = 0;
      state.released = false;
    }
  }
}

void InputMapId(InputMappingId mapping, InputId id) {
  inputmapping.emplace(mapping, id);
}

bool Pressed(InputMappingId mapping) {
  auto id = inputmapping.at(mapping);
  auto it = inputstate.find(id);
  if (it == inputstate.end()) {
    return false;
  }

  auto& [_, state] = *it;
  return state.pressed_at > 0;
}

uint32_t PressedFor(InputMappingId mapping, uint64_t now) {
  auto id = inputmapping.at(mapping);
  auto it = inputstate.find(id);
  if (it == inputstate.end()) {
    return 0;
  }

  auto& [_, state] = *it;
  if (!state.pressed_at) {
    return 0;
  }

  return now - state.pressed_at + 1;
}

}  // namespace nyla