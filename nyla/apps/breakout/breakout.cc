#include "nyla/apps/breakout/breakout.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>

#include "nyla/commons/containers/set.h"
#include "nyla/commons/math/lerp.h"
#include "nyla/commons/math/math.h"
#include "nyla/commons/math/vec/vec2f.h"
#include "nyla/commons/os/clock.h"
#include "nyla/fwk/input.h"
#include "nyla/vulkan/vulkan.h"

namespace nyla {

#define X(key) const InputMappingId k##key;
NYLA_SHIPGAME_INPUT_MAPPING(X)
#undef X

void ProcessInput() {
  const int dx = Pressed(kRight) - Pressed(kLeft);

  static float dt_accumulator = 0.f;
  dt_accumulator += vk.current_frame_data.dt;

  constexpr float step = 1.f / 120.f;
  for (; dt_accumulator >= step; dt_accumulator -= step) {
  }
}

void BreakoutInit() {}

}  // namespace nyla