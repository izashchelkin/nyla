#include <cstdint>
#include <vector>

#include "nyla/commons/clock.h"

namespace nyla {

template <size_t FramesToKeep, size_t MaxStagesPerFrame>
struct ProfilingData {
  size_t iframe;
  std::array<uint64_t, FramesToKeep> last_stage_starts;
  std::array<uint16_t, MaxStagesPerFrame * FramesToKeep> stage_durations;
};

template <size_t FramesToKeep, size_t MaxStagesPerFrame>
void ProfilingFrameBegin(
    ProfilingData<FramesToKeep, MaxStagesPerFrame>& prof_data) {
  prof_data.iframe = (prof_data.iframe + 1) % FramesToKeep;
  prof_data.last_stage_starts[prof_data.iframe] = GetMonotonicTimeNanos();
}

#if 0
template <size_t FramesToKeep, size_t MaxStagesPerFrame>
void ProfilingStageBegin(
    ProfilingData<FramesToKeep, MaxStagesPerFrame>& prof_data) {
	prof_data
  prof_data.last_stage_starts[prof_data.iframe] = GetMonotonicTimeNanos();
}
#endif

}  // namespace nyla
