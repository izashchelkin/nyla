#pragma once

#include <cstdint>

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"

namespace nyla
{

// Per-window input for infinite-strip layout computation.
struct wm_layout_input
{
    uint32_t desiredWidth; // user-set pixel width (default: kWmDefaultWindowWidth)
    uint32_t maxWidth;     // client's WM_NORMAL_HINTS max width; 0 = unlimited
};

// Default window width when the client doesn't specify one.
constexpr uint32_t kWmDefaultWindowWidth = 1280;

// Compute infinite-strip horizontal tiling positions.
//
// Windows are placed left-to-right at their desiredWidth pixels (capped to
// viewW and client maxWidth, minimum 100px). Each window's X position is the
// cumulative sum of previous widths.
//
// Caller is responsible for:
//   1. Providing inputs in left-to-right stack order
//   2. Clearing winX and winW before calling (they're appended to)
void ComputeInfiniteStripLayout(span<const wm_layout_input> windows, int32_t viewW, inline_vec<int32_t, 64> &winX,
                                inline_vec<uint32_t, 64> &winW);

} // namespace nyla
