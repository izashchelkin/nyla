#pragma once

#include <cstdint>
#include <vector>

#include "absl/strings/str_format.h"

namespace nyla
{

class Rect
{
  public:
    Rect(int32_t x, int32_t y, uint32_t width, uint32_t height) : x_{x}, y_{y}, width_{width}, height_{height}
    {
    }
    Rect(uint32_t width, uint32_t height) : Rect{0, 0, width, height}
    {
    }
    Rect() : Rect{0, 0, 0, 0}
    {
    }

    void operator=(const Rect &rhs)
    {
        this->x_ = rhs.x_;
        this->y_ = rhs.y_;
        this->width_ = rhs.width_;
        this->height_ = rhs.height_;
    }

    auto operator==(const Rect &rhs) const -> bool
    {
        return this->x_ == rhs.x_ && this->y_ == rhs.y_ && this->width_ == rhs.width_ && this->height_ == rhs.height_;
    }
    auto operator!=(const Rect &rhs) const -> bool
    {
        return !(*this == rhs);
    }

    template <typename Sink> friend void AbslStringify(Sink &sink, const Rect &rect);

    auto x() -> int32_t &
    {
        return x_;
    }
    auto x() const -> const int32_t &
    {
        return x_;
    }

    auto y() -> int32_t &
    {
        return y_;
    }
    auto y() const -> const int32_t &
    {
        return y_;
    }

    auto width() -> uint32_t &
    {
        return width_;
    }
    auto width() const -> const uint32_t &
    {
        return width_;
    }

    auto height() -> uint32_t &
    {
        return height_;
    }
    auto height() const -> const uint32_t &
    {
        return height_;
    }

  private:
    int32_t x_;
    int32_t y_;
    uint32_t width_;
    uint32_t height_;
};
static_assert(sizeof(Rect) == 16);

template <typename Sink> void AbslStringify(Sink &sink, const Rect &rect)
{
    absl::Format(&sink, "%dx%d at (%d, %d)", rect.width_, rect.height_, rect.x_, rect.y_);
}

inline auto IsSameWH(const Rect &lhs, const Rect &rhs) -> bool
{
    if (lhs.width() != rhs.width())
        return false;
    if (lhs.height() != rhs.height())
        return false;
    return true;
}

auto TryApplyPadding(const Rect &rect, uint32_t padding) -> Rect;
auto TryApplyMargin(const Rect &rect, uint32_t margin) -> Rect;
auto TryApplyMarginTop(const Rect &rect, uint32_t margin_top) -> Rect;

enum class LayoutType
{
    kColumns,
    kRows,
    kGrid,
    /* kFibonacciSpiral */
};

void CycleLayoutType(LayoutType &layout);

auto ComputeLayout(const Rect &bounding_rect, uint32_t n, uint32_t padding,
                                LayoutType layout_type = LayoutType::kColumns) -> std::vector<Rect>;

} // namespace nyla