#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"

namespace nyla
{

enum class X11ErrorCode : uint8_t
{
    Success = 0,           /* everything's okay */
    BadRequest = 1,        /* bad request code */
    BadValue = 2,          /* int parameter out of range */
    BadWindow = 3,         /* parameter not a Window */
    BadPixmap = 4,         /* parameter not a Pixmap */
    BadAtom = 5,           /* parameter not an Atom */
    BadCursor = 6,         /* parameter not a Cursor */
    BadFont = 7,           /* parameter not a Font */
    BadMatch = 8,          /* parameter mismatch */
    BadDrawable = 9,       /* parameter not a Pixmap or Window */
    BadAccess = 10,        /* depending on context:
                                 - key/button already grabbed
                                 - attempt to free an illegal
                                   cmap entry
                                - attempt to store into a read-only
                                   color map entry.
                                - attempt to modify the access control
                                   list from other than the local host.
                                */
    BadAlloc = 11,         /* insufficient resources */
    BadColor = 12,         /* no such colormap */
    BadGc = 13,            /* parameter not a GC */
    BadIdChoice = 14,      /* choice not in range or already used */
    BadName = 15,          /* font or color name doesn't exist */
    BadLength = 16,        /* Request length incorrect */
    BadImplementation = 17 /* server is defective */
};

template <typename Sink> void AbslStringify(Sink &sink, X11ErrorCode errorCode)
{
    switch (errorCode)
    {
    case X11ErrorCode::Success:
        absl::Format(&sink, "Success");
        return;
    case X11ErrorCode::BadRequest:
        absl::Format(&sink, "BadRequest");
        return;
    case X11ErrorCode::BadValue:
        absl::Format(&sink, "BadValue");
        return;
    case X11ErrorCode::BadWindow:
        absl::Format(&sink, "BadWindow");
        return;
    case X11ErrorCode::BadPixmap:
        absl::Format(&sink, "BadPixmap");
        return;
    case X11ErrorCode::BadAtom:
        absl::Format(&sink, "BadAtom");
        return;
    case X11ErrorCode::BadCursor:
        absl::Format(&sink, "BadCursor");
        return;
    case X11ErrorCode::BadFont:
        absl::Format(&sink, "BadFont");
        return;
    case X11ErrorCode::BadMatch:
        absl::Format(&sink, "BadMatch");
        return;
    case X11ErrorCode::BadDrawable:
        absl::Format(&sink, "BadDrawable");
        return;
    case X11ErrorCode::BadAccess:
        absl::Format(&sink, "BadAccess");
        return;
    case X11ErrorCode::BadAlloc:
        absl::Format(&sink, "BadAlloc");
        return;
    case X11ErrorCode::BadColor:
        absl::Format(&sink, "BadColor");
        return;
    case X11ErrorCode::BadGc:
        absl::Format(&sink, "BadGC");
        return;
    case X11ErrorCode::BadIdChoice:
        absl::Format(&sink, "BadIDChoice");
        return;
    case X11ErrorCode::BadName:
        absl::Format(&sink, "BadName");
        return;
    case X11ErrorCode::BadLength:
        absl::Format(&sink, "BadLength");
        return;
    case X11ErrorCode::BadImplementation:
        absl::Format(&sink, "BadImplementation");
        return;
    }

    absl::Format(&sink, "bad error code");
}

} // namespace nyla