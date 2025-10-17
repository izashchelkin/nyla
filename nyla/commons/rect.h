#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"

namespace nyla {

class Rect {
 public:
  Rect(int32_t x, int32_t y, uint32_t width, uint32_t height)
      : x_{x}, y_{y}, width_{width}, height_{height} {}
  Rect(uint32_t width, uint32_t height) : Rect{0, 0, width, height} {}
  Rect() : Rect{0, 0, 0, 0} {}

  void operator=(const Rect& rhs) {
    this->x_ = rhs.x_;
    this->y_ = rhs.y_;
    this->width_ = rhs.width_;
    this->height_ = rhs.height_;
  }

  bool operator==(const Rect& rhs) const {
    return this->x_ == rhs.x_ && this->y_ == rhs.y_ &&
           this->width_ == rhs.width_ && this->height_ == rhs.height_;
  }
  bool operator!=(const Rect& rhs) const { return !(*this == rhs); }

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
static_assert(sizeof(Rect) == 16);

template <typename Sink>
void AbslStringify(Sink& sink, const Rect& rect) {
  absl::Format(&sink, "%dx%d at (%d, %d)", rect.width_, rect.height_, rect.x_,
               rect.y_);
}

inline bool IsSameWH(const Rect& lhs, const Rect& rhs) {
  if (lhs.width() != rhs.width()) return false;
  if (lhs.height() != rhs.height()) return false;
  return true;
}

Rect TryApplyPadding(const Rect& rect, uint32_t padding);
Rect TryApplyMargin(const Rect& rect, uint32_t margin);
Rect TryApplyMarginTop(const Rect& rect, uint32_t margin_top);

}  // namespace nyla
