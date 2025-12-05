#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"

namespace nyla
{

namespace platform_x11_internal
{

enum class X11ErrorCode : uint8_t
{
    KSuccess = 0,           /* everything's okay */
    KBadRequest = 1,        /* bad request code */
    KBadValue = 2,          /* int parameter out of range */
    KBadWindow = 3,         /* parameter not a Window */
    KBadPixmap = 4,         /* parameter not a Pixmap */
    KBadAtom = 5,           /* parameter not an Atom */
    KBadCursor = 6,         /* parameter not a Cursor */
    KBadFont = 7,           /* parameter not a Font */
    KBadMatch = 8,          /* parameter mismatch */
    KBadDrawable = 9,       /* parameter not a Pixmap or Window */
    KBadAccess = 10,        /* depending on context:
                                  - key/button already grabbed
                                  - attempt to free an illegal
                                    cmap entry
                                 - attempt to store into a read-only
                                    color map entry.
                                 - attempt to modify the access control
                                    list from other than the local host.
                                 */
    KBadAlloc = 11,         /* insufficient resources */
    KBadColor = 12,         /* no such colormap */
    KBadGc = 13,            /* parameter not a GC */
    KBadIdChoice = 14,      /* choice not in range or already used */
    KBadName = 15,          /* font or color name doesn't exist */
    KBadLength = 16,        /* Request length incorrect */
    KBadImplementation = 17 /* server is defective */
};

template <typename Sink> void AbslStringify(Sink &sink, X11ErrorCode errorCode)
{
    switch (errorCode)
    {
    case X11ErrorCode::KSuccess:
        absl::Format(&sink, "Success");
        return;
    case X11ErrorCode::KBadRequest:
        absl::Format(&sink, "BadRequest");
        return;
    case X11ErrorCode::KBadValue:
        absl::Format(&sink, "BadValue");
        return;
    case X11ErrorCode::KBadWindow:
        absl::Format(&sink, "BadWindow");
        return;
    case X11ErrorCode::KBadPixmap:
        absl::Format(&sink, "BadPixmap");
        return;
    case X11ErrorCode::KBadAtom:
        absl::Format(&sink, "BadAtom");
        return;
    case X11ErrorCode::KBadCursor:
        absl::Format(&sink, "BadCursor");
        return;
    case X11ErrorCode::KBadFont:
        absl::Format(&sink, "BadFont");
        return;
    case X11ErrorCode::KBadMatch:
        absl::Format(&sink, "BadMatch");
        return;
    case X11ErrorCode::KBadDrawable:
        absl::Format(&sink, "BadDrawable");
        return;
    case X11ErrorCode::KBadAccess:
        absl::Format(&sink, "BadAccess");
        return;
    case X11ErrorCode::KBadAlloc:
        absl::Format(&sink, "BadAlloc");
        return;
    case X11ErrorCode::KBadColor:
        absl::Format(&sink, "BadColor");
        return;
    case X11ErrorCode::KBadGc:
        absl::Format(&sink, "BadGC");
        return;
    case X11ErrorCode::KBadIdChoice:
        absl::Format(&sink, "BadIDChoice");
        return;
    case X11ErrorCode::KBadName:
        absl::Format(&sink, "BadName");
        return;
    case X11ErrorCode::KBadLength:
        absl::Format(&sink, "BadLength");
        return;
    case X11ErrorCode::KBadImplementation:
        absl::Format(&sink, "BadImplementation");
        return;
    }

    absl::Format(&sink, "bad error code");
}

} // namespace platform_x11_internal

} // namespace nyla