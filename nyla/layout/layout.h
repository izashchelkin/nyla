#pragma once

#include <cstdint>
#include <vector>

#include "nyla/rect/rect.h"

namespace nyla {

enum class LayoutType {
  kColumns,
  kRows,
  kGrid,
  /* kFibonacciSpiral */
};

void CycleLayoutType(LayoutType& layout);

std::vector<Rect> ComputeLayout(const Rect& bounding_rect, uint32_t n,
                                uint32_t padding,
                                LayoutType layout_type = LayoutType::kColumns);

}  // namespace nyla
