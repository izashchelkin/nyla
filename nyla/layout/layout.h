#ifndef NYLA_LAYOUT_H
#define NYLA_LAYOUT_H

#include <cstdint>
#include <vector>

#include "nyla/layout/rect.h"

namespace nyla {

enum class LayoutType { kColumns, kRows, /* kGrid, kFibonacciSpiral */ };

std::vector<Rect> LayoutComputeRectPlacements(
    const Rect& bounding_rect, uint32_t n,
    LayoutType layout_type = LayoutType::kColumns);

}  // namespace nyla

#endif
