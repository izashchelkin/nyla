#pragma once

#include <cstdint>
namespace nyla
{

auto RecompileShadersIfNeeded() -> bool;
void UpdateDtFps(uint32_t &fps, float &dt);

} // namespace nyla