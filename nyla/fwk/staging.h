#pragma once

#include <cstdint>
namespace nyla {

bool RecompileShadersIfNeeded();
void UpdateDtFps(uint32_t& fps, float& dt);

}  // namespace nyla