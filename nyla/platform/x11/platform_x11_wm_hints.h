#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"
#include "xcb/xproto.h"

namespace nyla
{

namespace platform_x11_internal
{

struct WmHints
{
    static constexpr uint32_t kInputHint = 1;
    static constexpr uint32_t kStateHint = 1 << 1;
    static constexpr uint32_t kIconPixmapHint = 1 << 2;
    static constexpr uint32_t kIconWindowHint = 1 << 3;
    static constexpr uint32_t kIconPositionHint = 1 << 4;
    static constexpr uint32_t kIconMaskHint = 1 << 5;
    static constexpr uint32_t kWindowGroupHint = 1 << 6;
    static constexpr uint32_t kUrgencyHint = 1 << 8;

    uint32_t flags;
    bool input;
    uint32_t initialState;
    xcb_pixmap_t iconPixmap;
    xcb_window_t iconWindow;
    int32_t iconX;
    int32_t iconY;
    xcb_pixmap_t iconMask;
    xcb_window_t windowGroup;

    auto Urgent() const -> bool
    {
        return flags & WmHints::kUrgencyHint;
    }
};
static_assert(sizeof(WmHints) == 9 * 4);

void Initialize(WmHints &h);

template <typename Sink> void AbslStringify(Sink &sink, const WmHints &h)
{
    absl::Format(&sink,
                 "WM_Hints {flags=%v input=%v initial_state=%v icon_pixmap=%v "
                 "icon_window=%v icon_x=%v icon_y=%v icon_mask=%v window_group=%v}",
                 h.flags, h.input, h.initialState, h.iconPixmap, h.iconWindow, h.iconX, h.iconY, h.iconMask,
                 h.windowGroup);
}

struct WmNormalHints
{
    static constexpr uint32_t kPMinSize = 1 << 4;
    static constexpr uint32_t kPMaxSize = 1 << 5;
    static constexpr uint32_t kPResizeInc = 1 << 6;
    static constexpr uint32_t kPAspect = 1 << 7;
    static constexpr uint32_t kPBaseSize = 1 << 8;
    static constexpr uint32_t kPWinGravity = 1 << 9;

    uint32_t flags;
    uint32_t pad[4];
    int32_t minWidth, minHeight;
    int32_t maxWidth, maxHeight;
    int32_t widthInc, heightInc;
    struct
    {
        int num;
        int den;
    } minAspect, maxAspect;
    int32_t baseWidth, baseHeight;
    xcb_gravity_t winGravity;
};
static_assert(sizeof(WmNormalHints) == 18 * 4);

void Initialize(WmNormalHints &h);

template <typename Sink> void AbslStringify(Sink &sink, const WmNormalHints &h)
{
    absl::Format(&sink,
                 "WM_Normal_Hints {flags=%v min_width=%v min_height=%v "
                 "max_width=%v max_height=%v width_inc=%v "
                 "height_inc=%v min_aspect=%v/%v max_aspect=%v/%v base_width=%v "
                 "base_height=%v "
                 "win_gravity=%v}",
                 h.flags, h.minWidth, h.minHeight, h.maxWidth, h.maxHeight, h.widthInc, h.heightInc, h.minAspect.num,
                 h.minAspect.den, h.maxAspect.num, h.maxAspect.den, h.baseWidth, h.baseHeight, h.winGravity);
}

} // namespace platform_x11_internal

} // namespace nyla