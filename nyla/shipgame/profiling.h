#include <cstdint>
#include <vector>

#include "nyla/commons/clock.h"

namespace nyla {

template <size_t FramesToKeep, size_t MaxStagesPerFrame>
struct ProfilingData {
  size_t i;
  std::array<uint64_t, FramesToKeep> starts;
  std::array<uint16_t, MaxStagesPerFrame * FramesToKeep> stage_durations;
};

template <size_t FramesToKeep, size_t MaxStagesPerFrame>
void ProfilingFrameBegin(
    ProfilingData<FramesToKeep, MaxStagesPerFrame>& prof_data) {
  prof_data.i = (prof_data.i + 1) % FramesToKeep;
	prof_data.starts[prof_data.i] = GetMonotonicTimeNanos();
}

void ProfilingStageBegin();

}  // namespace nyla
