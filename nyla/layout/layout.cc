#include "nyla/layout/layout.h"

#include <cstdint>
#include <vector>

#include "nyla/layout/rect.h"

namespace nyla {

static void ComputeColumns(const Rect& bounding_rect, uint32_t n,
                           std::vector<Rect>& out) {
  uint32_t width = bounding_rect.width / n;
  for (uint32_t i = 0; i < n; ++i) {
    out.emplace_back(Rect{.x = bounding_rect.x + (i * width),
                          .y = bounding_rect.y,
                          .width = width,
                          .height = bounding_rect.height});
  }
}

static void ComputeRows(const Rect& bounding_rect, uint32_t n,
                        std::vector<Rect>& out) {
  uint32_t height = bounding_rect.height / n;
  for (uint32_t i = 0; i < n; ++i) {
    out.emplace_back(Rect{.x = bounding_rect.x,
                          .y = bounding_rect.y + (i * height),
                          .width = bounding_rect.width,
                          .height = height});
  }
}

std::vector<Rect> LayoutComputeRectPlacements(const Rect& bounding_rect,
                                              uint32_t n,
                                              LayoutType layout_type) {
  switch (n) {
    case 0:
      return {};
    case 1:
      return {bounding_rect};  // no gaps!
  }

  std::vector<Rect> rects;
  rects.reserve(n);
  switch (layout_type) {
    case LayoutType::kColumns:
      ComputeColumns(bounding_rect, n, rects);
      break;
    case LayoutType::kRows:
      ComputeRows(bounding_rect, n, rects);
      break;
  }
  return rects;
}

}  // namespace nyla
