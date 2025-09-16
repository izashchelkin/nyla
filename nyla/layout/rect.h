#ifndef NYLA_LAYOUT_RECT_H
#define NYLA_LAYOUT_RECT_H

#include <cstdint>

#include "absl/strings/str_format.h"

struct Rect {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
};

template <typename Sink>
void AbslStringify(Sink& sink, const Rect& rect) {
  absl::Format(&sink, "%dx%d at (%d, %d)", rect.width, rect.height, rect.x,
               rect.y);
}

inline bool operator==(const Rect& lhs, const Rect& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
         lhs.height == rhs.height;
}

#endif
