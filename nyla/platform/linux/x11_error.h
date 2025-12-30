#pragma once

#include <cstdint>

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

} // namespace nyla