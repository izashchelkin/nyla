#pragma once

#include <cstdint>

#include "nyla/platform/platform.h"

namespace nyla
{

constexpr inline uint32_t kRhiMaxNumFramesInFlight = 3;

#if defined(NDEBUG)
constexpr inline bool kRhiValidations = false;
#else
constexpr inline bool kRhiValidations = true;
#endif

constexpr inline bool kRhiCheckpoints = false;

//

struct RhiDesc
{
    PlatformWindow window;
    uint32_t numFramesInFlight;
};

void RhiInit(const RhiDesc &);
auto RhiGetNumFramesInFlight() -> uint32_t;
auto RhiFrameGetIndex() -> uint32_t;

auto RhiGetSurfaceWidth() -> uint32_t;
auto RhiGetSurfaceHeight() -> uint32_t;

auto RhiGetMinUniformBufferOffsetAlignment() -> uint32_t;

} // namespace nyla