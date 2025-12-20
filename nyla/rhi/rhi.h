#pragma once

#include <cstdint>

#include "nyla/commons/bitenum.h"
#include "nyla/platform/platform.h"

namespace nyla
{

constexpr inline uint32_t kRhiMaxNumFramesInFlight = 3;
constexpr inline uint32_t kRhiMaxNumSwapchainTextures = 4;

#if defined(NDEBUG)
constexpr inline bool kRhiValidations = false;
#else
constexpr inline bool kRhiValidations = true;
#endif

constexpr inline bool kRhiCheckpoints = false;

//

enum class RhiFlags : uint32_t
{
    VSync = 1 << 0,
};

template <> struct EnableBitMaskOps<RhiFlags> : std::true_type
{
};

struct RhiDesc
{
    RhiFlags flags;

    PlatformWindow window;
    uint32_t numFramesInFlight;
};

void RhiInit(const RhiDesc &);
auto RhiGetNumFramesInFlight() -> uint32_t;
auto RhiGetFrameIndex() -> uint32_t;
auto RhiGetMinUniformBufferOffsetAlignment() -> uint32_t;
auto RhiGetOptimalBufferCopyOffsetAlignment() -> uint32_t;

} // namespace nyla