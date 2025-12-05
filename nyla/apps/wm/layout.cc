#include "nyla/apps/wm/layout.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace nyla
{

void CycleLayoutType(LayoutType &layout)
{
    switch (layout)
    {
    case LayoutType::kColumns:
        layout = LayoutType::kRows;
        break;
    case LayoutType::kRows:
        layout = LayoutType::kGrid;
        break;
    case LayoutType::kGrid:
        layout = LayoutType::kColumns;
        break;

    default:
        std::unreachable();
    }
}

static void ComputeColumns(const Rect &bounding_rect, uint32_t n, uint32_t padding, std::vector<Rect> &out)
{
    uint32_t width = bounding_rect.width() / n;
    for (uint32_t i = 0; i < n; ++i)
    {
        out.emplace_back(TryApplyPadding(Rect(static_cast<int32_t>(bounding_rect.x() + (i * width)), bounding_rect.y(),
                                              width, bounding_rect.height()),
                                         padding));
    }
}

static void ComputeRows(const Rect &bounding_rect, uint32_t n, uint32_t padding, std::vector<Rect> &out)
{
    uint32_t height = bounding_rect.height() / n;
    for (uint32_t i = 0; i < n; ++i)
    {
        out.emplace_back(TryApplyPadding(Rect(bounding_rect.x(), static_cast<int32_t>(bounding_rect.y() + (i * height)),
                                              bounding_rect.width(), height),
                                         padding));
    }
}

static void ComputeGrid(const Rect &bounding_rect, uint32_t n, uint32_t padding, std::vector<Rect> &out)
{
    if (n < 4)
    {
        ComputeColumns(bounding_rect, n, padding, out);
    }
    else
    {
        double root = std::sqrt(n);
        uint32_t num_cols = std::floor(root);
        uint32_t num_rows = std::ceil(n / (double)num_cols);

        uint32_t width = bounding_rect.width() / num_cols;
        uint32_t height = bounding_rect.height() / num_rows;

        for (uint32_t i = 0; i < n; ++i)
        {
            out.emplace_back(TryApplyPadding(Rect(static_cast<int32_t>(bounding_rect.x() + ((i % num_cols) * width)),
                                                  static_cast<int32_t>(bounding_rect.y() + ((i / num_cols) * height)),
                                                  width, height),
                                             padding));
        }
    }
}

auto ComputeLayout(const Rect &bounding_rect, uint32_t n, uint32_t padding, LayoutType layout_type) -> std::vector<Rect>
{
    switch (n)
    {
    case 0:
        return {};
    case 1:
        return {TryApplyPadding(bounding_rect, padding)};
    }

    std::vector<Rect> out;
    out.reserve(n);
    switch (layout_type)
    {
    case LayoutType::kColumns:
        ComputeColumns(bounding_rect, n, padding, out);
        break;
    case LayoutType::kRows:
        ComputeRows(bounding_rect, n, padding, out);
        break;
    case LayoutType::kGrid:
        ComputeGrid(bounding_rect, n, padding, out);
        break;
    }
    return out;
}

auto TryApplyPadding(const Rect &rect, uint32_t padding) -> Rect
{
    if (rect.width() > 2 * padding && rect.height() > 2 * padding)
    {
        return Rect(rect.x(), rect.y(), rect.width() - 2 * padding, rect.height() - 2 * padding);
    }
    else
    {
        return rect;
    }
}

auto TryApplyMarginTop(const Rect &rect, uint32_t margin_top) -> Rect
{
    if (rect.height() > margin_top)
    {
        return Rect(rect.x(), rect.y() + margin_top, rect.width(), rect.height() - margin_top);
    }
    else
    {
        return rect;
    }
}

auto TryApplyMargin(const Rect &rect, uint32_t margin) -> Rect
{
    if (rect.width() > 2 * margin && rect.height() > 2 * margin)
    {
        return Rect(rect.x() + margin, rect.y() + margin, rect.width() - 2 * margin, rect.height() - 2 * margin);
    }
    else
    {
        return rect;
    }
}

} // namespace nyla