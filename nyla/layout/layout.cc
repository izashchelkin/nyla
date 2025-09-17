#include "nyla/layout/layout.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "nyla/layout/rect.h"

namespace nyla {

static void ComputeColumns(const Rect& bounding_rect, uint32_t n,
                           uint32_t margin, std::vector<Rect>& out) {
  uint32_t width = bounding_rect.width / n;
  for (uint32_t i = 0; i < n; ++i) {
    out.emplace_back(ApplyMargin(
        Rect{.x = static_cast<int32_t>(bounding_rect.x + (i * width)),
             .y = bounding_rect.y,
             .width = width,
             .height = bounding_rect.height},
        margin));
  }
}

static void ComputeRows(const Rect& bounding_rect, uint32_t n, uint32_t margin,
                        std::vector<Rect>& out) {
  uint32_t height = bounding_rect.height / n;
  for (uint32_t i = 0; i < n; ++i) {
    out.emplace_back(ApplyMargin(
        Rect{.x = bounding_rect.x,
             .y = static_cast<int32_t>(bounding_rect.y + (i * height)),
             .width = bounding_rect.width,
             .height = height},
        margin));
  }
}

static void ComputeGrid(const Rect& bounding_rect, uint32_t n, uint32_t margin,
                        std::vector<Rect>& out) {
  if (n < 4) {
    ComputeColumns(bounding_rect, n, margin, out);
    return;
  }

  double root = std::sqrt(n);
  uint32_t num_cols = std::floor(root);
  uint32_t num_rows = std::ceil(n / (double)num_cols);

  uint32_t width = bounding_rect.width / num_cols;
  uint32_t height = bounding_rect.height / num_rows;

  for (uint32_t i = 0; i < n; ++i) {
    out.emplace_back(
        ApplyMargin(Rect{.x = static_cast<int32_t>(bounding_rect.x +
                                                   ((i % num_cols) * width)),
                         .y = static_cast<int32_t>(bounding_rect.y +
                                                   ((i / num_cols) * height)),
                         .width = width,
                         .height = height},
                    margin));
  }
}

std::vector<Rect> ComputeLayout(const Rect& bounding_rect, uint32_t n,
                                uint32_t margin, LayoutType layout_type) {
  switch (n) {
    case 0:
      return {};
    case 1:
      return {ApplyMargin(bounding_rect, margin)};
  }

  std::vector<Rect> out;
  out.reserve(n);
  switch (layout_type) {
    case LayoutType::kColumns:
      ComputeColumns(bounding_rect, n, margin, out);
      break;
    case LayoutType::kRows:
      ComputeRows(bounding_rect, n, margin, out);
      break;
    case LayoutType::kGrid:
      ComputeGrid(bounding_rect, n, margin, out);
      break;
  }
  return out;
}

}  // namespace nyla
