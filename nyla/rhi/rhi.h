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
NYLA_BITENUM(RhiFlags);

struct RhiInitDesc
{
    RhiFlags flags;

    PlatformWindow window;
    uint32_t numFramesInFlight;
};

class Rhi
{
  public:
    void Init(const RhiInitDesc &);
    auto GetNumFramesInFlight() -> uint32_t;
    auto GetFrameIndex() -> uint32_t;
    auto GetMinUniformBufferOffsetAlignment() -> uint32_t;
    auto GetOptimalBufferCopyOffsetAlignment() -> uint32_t;

  private:
    class Impl;
    Impl *impl;
};

} // namespace nyla