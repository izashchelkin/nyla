#pragma once

#include <cstdint>
namespace nyla
{

auto RecompileShadersIfNeeded() -> bool;
void UpdateDtFps(uint64_t nowUs, uint32_t &fps, float &dt);

} // namespace nyla