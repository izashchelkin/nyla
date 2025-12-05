#pragma once

#include <cstdint>

#include "nyla/platform/platform.h"

namespace nyla
{

constexpr inline uint32_t rhi_max_num_frames_in_flight = 3;

#if defined(NDEBUG)
constexpr inline bool rhi_validations = false;
#else
constexpr inline bool rhi_validations = true;
#endif

constexpr inline bool rhi_checkpoints = false;

//

struct RhiDesc
{
    PlatformWindow window;
    uint32_t num_frames_in_flight;
};

void RhiInit(const RhiDesc &);
auto RhiGetNumFramesInFlight() -> uint32_t;
auto RhiFrameGetIndex() -> uint32_t;

auto RhiGetSurfaceWidth() -> uint32_t;
auto RhiGetSurfaceHeight() -> uint32_t;

auto RhiGetMinUniformBufferOffsetAlignment() -> uint32_t;

} // namespace nyla