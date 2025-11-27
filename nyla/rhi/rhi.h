#pragma once

#include <cstdint>

namespace nyla {

struct RhiHandle {
  uint32_t gen;
  uint32_t index;
};

enum class RhiQueue {
  GRAPHICS,
  TRANSFER,
};

struct RhiSwapchain : RhiHandle {};
struct RhiTimeline : RhiHandle {};
struct RhiPipeline : RhiHandle {};
struct RhiCmdList : RhiHandle {};
struct RhiShader : RhiHandle {};

struct RhiDesc {
  constexpr static uint32_t kMaxFramesInFlight = 3;

  uint32_t num_frames_in_flight;
};

struct Rhi {
  void (*Init)(RhiDesc);
};
extern Rhi rhi;

}  // namespace nyla