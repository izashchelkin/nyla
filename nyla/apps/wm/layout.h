#pragma once

#include <cstdint>
#include <vector>

namespace nyla
{

class Rect
{
  public:
    Rect(int32_t x, int32_t y, uint32_t width, uint32_t height) : x{x}, y{y}, width{width}, height{height}
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
        this->x = rhs.x;
        this->y = rhs.y;
        this->width = rhs.width;
        this->height = rhs.height;
    }

    auto operator==(const Rect &rhs) const -> bool
    {
        return this->x == rhs.x && this->y == rhs.y && this->width == rhs.width && this->height == rhs.height;
    }
    auto operator!=(const Rect &rhs) const -> bool
    {
        return !(*this == rhs);
    }

    auto X() -> int32_t &
    {
        return x;
    }
    auto X() const -> const int32_t &
    {
        return x;
    }

    auto Y() -> int32_t &
    {
        return y;
    }
    auto Y() const -> const int32_t &
    {
        return y;
    }

    auto Width() -> uint32_t &
    {
        return width;
    }
    auto Width() const -> const uint32_t &
    {
        return width;
    }

    auto Height() -> uint32_t &
    {
        return height;
    }
    auto Height() const -> const uint32_t &
    {
        return height;
    }

  private:
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(Rect) == 16);

inline auto IsSameWH(const Rect &lhs, const Rect &rhs) -> bool
{
    if (lhs.Width() != rhs.Width())
        return false;
    if (lhs.Height() != rhs.Height())
        return false;
    return true;
}

auto TryApplyPadding(const Rect &rect, uint32_t padding) -> Rect;
auto TryApplyMargin(const Rect &rect, uint32_t margin) -> Rect;
auto TryApplyMarginTop(const Rect &rect, uint32_t marginTop) -> Rect;

enum class LayoutType
{
    KColumns,
    KRows,
    KGrid,
    /* kFibonacciSpiral */
};

void CycleLayoutType(LayoutType &layout);

auto ComputeLayout(const Rect &boundingRect, uint32_t n, uint32_t padding, LayoutType layoutType = LayoutType::KColumns)
    -> std::vector<Rect>;

} // namespace nyla