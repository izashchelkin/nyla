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
uint32_t RhiGetNumFramesInFlight();
uint32_t RhiFrameGetIndex();

uint32_t RhiGetSurfaceWidth();
uint32_t RhiGetSurfaceHeight();

uint32_t RhiGetMinUniformBufferOffsetAlignment();

} // namespace nyla