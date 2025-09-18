#ifndef NYLA_RECT_H
#define NYLA_RECT_H

#include <cstdint>

#include "absl/strings/str_format.h"

namespace nyla {

class Rect {
 public:
  Rect(int32_t x, int32_t y, uint32_t width, uint32_t height)
      : x_{x}, y_{y}, width_{width}, height_{height} {}
  Rect(uint32_t width, uint32_t height) : Rect{0, 0, width, height} {}
  Rect() : Rect{0, 0, 0, 0} {}

  friend bool operator==(const Rect& lhs, const Rect& rhs);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Rect& rect);

  int32_t& x() { return x_; }
  const int32_t& x() const { return x_; }

  int32_t& y() { return y_; }
  const int32_t& y() const { return y_; }

  uint32_t& width() { return width_; }
  const uint32_t& width() const { return width_; }

  uint32_t& height() { return height_; }
  const uint32_t& height() const { return height_; }

 private:
  int32_t x_;
  int32_t y_;
  uint32_t width_;
  uint32_t height_;
};

template <typename Sink>
void AbslStringify(Sink& sink, const Rect& rect) {
  absl::Format(&sink, "%dx%d at (%d, %d)", rect.width_, rect.height_, rect.x_,
               rect.y_);
}

Rect ApplyPadding(const Rect& rect, uint32_t padding);
Rect ApplyMarginTop(const Rect& rect, uint32_t margin_top);

}  // namespace nyla

#endif
