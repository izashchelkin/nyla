#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"
#include "xcb/xproto.h"

namespace nyla
{

struct X11WmHints
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
        return flags & X11WmHints::kUrgencyHint;
    }
};
static_assert(sizeof(X11WmHints) == 9 * 4);

void Initialize(X11WmHints &h);

template <typename Sink> void AbslStringify(Sink &sink, const X11WmHints &h)
{
    absl::Format(&sink,
                 "WM_Hints {flags=%v input=%v initial_state=%v icon_pixmap=%v "
                 "icon_window=%v icon_x=%v icon_y=%v icon_mask=%v window_group=%v}",
                 h.flags, h.input, h.initialState, h.iconPixmap, h.iconWindow, h.iconX, h.iconY, h.iconMask,
                 h.windowGroup);
}

struct X11WmNormalHints
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
static_assert(sizeof(X11WmNormalHints) == 18 * 4);

void Initialize(X11WmNormalHints &h);

template <typename Sink> void AbslStringify(Sink &sink, const X11WmNormalHints &h)
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

inline void Initialize(X11WmHints &h)
{
    if (!(h.flags & X11WmHints::kInputHint))
    {
        h.input = true;
    }

    if (!(h.flags & X11WmHints::kStateHint))
    {
        h.initialState = 0;
    }

    if (!(h.flags & X11WmHints::kIconPixmapHint))
    {
        h.iconPixmap = 0;
    }

    if (!(h.flags & X11WmHints::kIconWindowHint))
    {
        h.iconWindow = 0;
    }

    if (!(h.iconX & X11WmHints::kIconPositionHint))
    {
        h.iconX = 0;
        h.iconY = 0;
    }

    if (!(h.iconX & X11WmHints::kIconMaskHint))
    {
        h.iconMask = 0;
    }
}

inline void Initialize(X11WmNormalHints &h)
{
    if (!(h.flags & X11WmNormalHints::kPMinSize))
    {
        h.minWidth = 0;
        h.minHeight = 0;
    }

    if (!(h.flags & X11WmNormalHints::kPMaxSize))
    {
        h.maxWidth = 0;
        h.maxHeight = 0;
    }

    if (!(h.flags & X11WmNormalHints::kPResizeInc))
    {
        h.widthInc = 0;
        h.heightInc = 0;
    }

    if (!(h.flags & X11WmNormalHints::kPAspect))
    {
        h.minAspect = {};
        h.maxAspect = {};
    }

    if (!(h.flags & X11WmNormalHints::kPBaseSize))
    {
        h.baseWidth = 0;
        h.baseHeight = 0;
    }

    if (!(h.flags & X11WmNormalHints::kPWinGravity))
    {
        h.winGravity = {};
    }
}

} // namespace nyla