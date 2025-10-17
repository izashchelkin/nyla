#include "nyla/commons/rect.h"

#include "absl/log/check.h"

namespace nyla {

Rect TryApplyPadding(const Rect& rect, uint32_t padding) {
  if (rect.width() > 2 * padding && rect.height() > 2 * padding) {
    return Rect(rect.x(), rect.y(), rect.width() - 2 * padding,
                rect.height() - 2 * padding);
  } else {
    return rect;
  }
}

Rect TryApplyMarginTop(const Rect& rect, uint32_t margin_top) {
  if (rect.height() > margin_top) {
    return Rect(rect.x(), rect.y() + margin_top, rect.width(),
                rect.height() - margin_top);
  } else {
    return rect;
  }
}

Rect TryApplyMargin(const Rect& rect, uint32_t margin) {
  if (rect.width() > 2 * margin && rect.height() > 2 * margin) {
    return Rect(rect.x() + margin, rect.y() + margin, rect.width() - 2 * margin,
                rect.height() - 2 * margin);
  } else {
    return rect;
  }
}

}  // namespace nyla
