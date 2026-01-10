#include "nyla/apps/wm/layout.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace nyla
{

void CycleLayoutType(LayoutType &layout)
{
    switch (layout)
    {
    case LayoutType::KColumns:
        layout = LayoutType::KRows;
        break;
    case LayoutType::KRows:
        layout = LayoutType::KGrid;
        break;
    case LayoutType::KGrid:
        layout = LayoutType::KColumns;
        break;

    default:
        std::unreachable();
    }
}

static void ComputeColumns(const Rect &boundingRect, uint32_t n, uint32_t padding, std::vector<Rect> &out)
{
    uint32_t width = boundingRect.Width() / n;
    for (uint32_t i = 0; i < n; ++i)
    {
        out.emplace_back(TryApplyPadding(
            Rect(static_cast<int32_t>(boundingRect.X() + (i * width)), boundingRect.Y(), width, boundingRect.Height()),
            padding));
    }
}

static void ComputeRows(const Rect &boundingRect, uint32_t n, uint32_t padding, std::vector<Rect> &out)
{
    uint32_t height = boundingRect.Height() / n;
    for (uint32_t i = 0; i < n; ++i)
    {
        out.emplace_back(TryApplyPadding(
            Rect(boundingRect.X(), static_cast<int32_t>(boundingRect.Y() + (i * height)), boundingRect.Width(), height),
            padding));
    }
}

static void ComputeGrid(const Rect &boundingRect, uint32_t n, uint32_t padding, std::vector<Rect> &out)
{
    if (n < 4)
    {
        ComputeColumns(boundingRect, n, padding, out);
    }
    else
    {
        double root = std::sqrt(n);
        uint32_t numCols = std::floor(root);
        uint32_t numRows = std::ceil(n / (double)numCols);

        uint32_t width = boundingRect.Width() / numCols;
        uint32_t height = boundingRect.Height() / numRows;

        for (uint32_t i = 0; i < n; ++i)
        {
            out.emplace_back(
                TryApplyPadding(Rect(static_cast<int32_t>(boundingRect.X() + ((i % numCols) * width)),
                                     static_cast<int32_t>(boundingRect.Y() + ((i / numCols) * height)), width, height),
                                padding));
        }
    }
}

auto ComputeLayout(const Rect &boundingRect, uint32_t n, uint32_t padding, LayoutType layoutType) -> std::vector<Rect>
{
    switch (n)
    {
    case 0:
        return {};
    case 1:
        return {TryApplyPadding(boundingRect, padding)};
    }

    std::vector<Rect> out;
    out.reserve(n);
    switch (layoutType)
    {
    case LayoutType::KColumns:
        ComputeColumns(boundingRect, n, padding, out);
        break;
    case LayoutType::KRows:
        ComputeRows(boundingRect, n, padding, out);
        break;
    case LayoutType::KGrid:
        ComputeGrid(boundingRect, n, padding, out);
        break;
    }
    return out;
}

auto TryApplyPadding(const Rect &rect, uint32_t padding) -> Rect
{
    if (rect.Width() > 2 * padding && rect.Height() > 2 * padding)
    {
        return Rect(rect.X(), rect.Y(), rect.Width() - 2 * padding, rect.Height() - 2 * padding);
    }
    else
    {
        return rect;
    }
}

auto TryApplyMarginTop(const Rect &rect, uint32_t marginTop) -> Rect
{
    if (rect.Height() > marginTop)
    {
        return Rect(rect.X(), rect.Y() + marginTop, rect.Width(), rect.Height() - marginTop);
    }
    else
    {
        return rect;
    }
}

auto TryApplyMargin(const Rect &rect, uint32_t margin) -> Rect
{
    if (rect.Width() > 2 * margin && rect.Height() > 2 * margin)
    {
        return Rect(rect.X() + margin, rect.Y() + margin, rect.Width() - 2 * margin, rect.Height() - 2 * margin);
    }
    else
    {
        return rect;
    }
}

} // namespace nyla