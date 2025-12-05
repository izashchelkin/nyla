#include "nyla/platform/x11/platform_x11_wm_hints.h"

namespace nyla
{

namespace platform_x11_internal
{

void Initialize(WmHints &h)
{
    if (!(h.flags & WmHints::kInputHint))
    {
        h.input = true;
    }

    if (!(h.flags & WmHints::kStateHint))
    {
        h.initialState = 0;
    }

    if (!(h.flags & WmHints::kIconPixmapHint))
    {
        h.iconPixmap = 0;
    }

    if (!(h.flags & WmHints::kIconWindowHint))
    {
        h.iconWindow = 0;
    }

    if (!(h.iconX & WmHints::kIconPositionHint))
    {
        h.iconX = 0;
        h.iconY = 0;
    }

    if (!(h.iconX & WmHints::kIconMaskHint))
    {
        h.iconMask = 0;
    }
}

void Initialize(WmNormalHints &h)
{
    if (!(h.flags & WmNormalHints::kPMinSize))
    {
        h.minWidth = 0;
        h.minHeight = 0;
    }

    if (!(h.flags & WmNormalHints::kPMaxSize))
    {
        h.maxWidth = 0;
        h.maxHeight = 0;
    }

    if (!(h.flags & WmNormalHints::kPResizeInc))
    {
        h.widthInc = 0;
        h.heightInc = 0;
    }

    if (!(h.flags & WmNormalHints::kPAspect))
    {
        h.minAspect = {};
        h.maxAspect = {};
    }

    if (!(h.flags & WmNormalHints::kPBaseSize))
    {
        h.baseWidth = 0;
        h.baseHeight = 0;
    }

    if (!(h.flags & WmNormalHints::kPWinGravity))
    {
        h.winGravity = {};
    }
}

} // namespace platform_x11_internal

} // namespace nyla