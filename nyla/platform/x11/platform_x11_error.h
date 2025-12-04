#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"

namespace nyla
{

namespace platform_x11_internal
{

enum class X11ErrorCode : uint8_t
{
    kSuccess = 0,           /* everything's okay */
    kBadRequest = 1,        /* bad request code */
    kBadValue = 2,          /* int parameter out of range */
    kBadWindow = 3,         /* parameter not a Window */
    kBadPixmap = 4,         /* parameter not a Pixmap */
    kBadAtom = 5,           /* parameter not an Atom */
    kBadCursor = 6,         /* parameter not a Cursor */
    kBadFont = 7,           /* parameter not a Font */
    kBadMatch = 8,          /* parameter mismatch */
    kBadDrawable = 9,       /* parameter not a Pixmap or Window */
    kBadAccess = 10,        /* depending on context:
                                  - key/button already grabbed
                                  - attempt to free an illegal
                                    cmap entry
                                 - attempt to store into a read-only
                                    color map entry.
                                 - attempt to modify the access control
                                    list from other than the local host.
                                 */
    kBadAlloc = 11,         /* insufficient resources */
    kBadColor = 12,         /* no such colormap */
    kBadGC = 13,            /* parameter not a GC */
    kBadIDChoice = 14,      /* choice not in range or already used */
    kBadName = 15,          /* font or color name doesn't exist */
    kBadLength = 16,        /* Request length incorrect */
    kBadImplementation = 17 /* server is defective */
};

template <typename Sink> void AbslStringify(Sink &sink, X11ErrorCode error_code)
{
    switch (error_code)
    {
    case X11ErrorCode::kSuccess:
        absl::Format(&sink, "Success");
        return;
    case X11ErrorCode::kBadRequest:
        absl::Format(&sink, "BadRequest");
        return;
    case X11ErrorCode::kBadValue:
        absl::Format(&sink, "BadValue");
        return;
    case X11ErrorCode::kBadWindow:
        absl::Format(&sink, "BadWindow");
        return;
    case X11ErrorCode::kBadPixmap:
        absl::Format(&sink, "BadPixmap");
        return;
    case X11ErrorCode::kBadAtom:
        absl::Format(&sink, "BadAtom");
        return;
    case X11ErrorCode::kBadCursor:
        absl::Format(&sink, "BadCursor");
        return;
    case X11ErrorCode::kBadFont:
        absl::Format(&sink, "BadFont");
        return;
    case X11ErrorCode::kBadMatch:
        absl::Format(&sink, "BadMatch");
        return;
    case X11ErrorCode::kBadDrawable:
        absl::Format(&sink, "BadDrawable");
        return;
    case X11ErrorCode::kBadAccess:
        absl::Format(&sink, "BadAccess");
        return;
    case X11ErrorCode::kBadAlloc:
        absl::Format(&sink, "BadAlloc");
        return;
    case X11ErrorCode::kBadColor:
        absl::Format(&sink, "BadColor");
        return;
    case X11ErrorCode::kBadGC:
        absl::Format(&sink, "BadGC");
        return;
    case X11ErrorCode::kBadIDChoice:
        absl::Format(&sink, "BadIDChoice");
        return;
    case X11ErrorCode::kBadName:
        absl::Format(&sink, "BadName");
        return;
    case X11ErrorCode::kBadLength:
        absl::Format(&sink, "BadLength");
        return;
    case X11ErrorCode::kBadImplementation:
        absl::Format(&sink, "BadImplementation");
        return;
    }

    absl::Format(&sink, "bad error code");
}

} // namespace platform_x11_internal

} // namespace nyla