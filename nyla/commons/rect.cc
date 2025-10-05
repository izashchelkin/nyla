#include "nyla/commons/rect.h"

#include "absl/log/check.h"

namespace nyla {

Rect ApplyPadding(const Rect& rect, uint32_t padding) {
  CHECK(rect.width() > 2 * padding && rect.height() > 2 * padding);

  return Rect(rect.x(), rect.y(), rect.width() - 2 * padding,
              rect.height() - 2 * padding);
}

Rect ApplyMarginTop(const Rect& rect, uint32_t margin_top) {
  CHECK(rect.height() > margin_top);

  return Rect(rect.x(), rect.y() + margin_top, rect.width(),
              rect.height() - margin_top);
}

Rect ApplyMargin(const Rect& rect, uint32_t margin) {
  CHECK(rect.width() > 2 * margin && rect.height() > 2 * margin);

  return Rect(rect.x() + margin, rect.y() + margin, rect.width() - 2 * margin,
              rect.height() - 2 * margin);
}

}  // namespace nyla
