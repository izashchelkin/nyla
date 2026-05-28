#include "nyla/commons/clipboard.h"

#include "nyla/commons/mem.h"
#include "nyla/commons/platform_linux.h" // IWYU pragma: keep

namespace nyla
{

namespace Clipboard
{

auto API Read(region_alloc &alloc) -> byteview
{
    // TODO: this needs some love

    xcb_window_t win = X11WinGetHandle();
    if (!win)
        return {};

    const x11_atoms &atoms = X11GetAtoms();
    xcb_atom_t prop = atoms.net_wm_name; // Reuse some atom for the property

    xcb_convert_selection(X11GetConn(), win, atoms.clipboard, atoms.utf8_string, prop, XCB_CURRENT_TIME);
    X11Flush();

    // Wait for SelectionNotify. We use a local event loop here.
    // In a real app we might want a better way, but for now this works.
    xcb_generic_event_t *event;
    while ((event = xcb_wait_for_event(X11GetConn())))
    {
        uint8_t type = event->response_type & 0x7F;
        if (type == XCB_SELECTION_NOTIFY)
        {
            auto *sn = reinterpret_cast<xcb_selection_notify_event_t *>(event);
            if (sn->property == XCB_NONE)
            {
                free(event);
                return {};
            }

            xcb_get_property_cookie_t cookie =
                xcb_get_property(X11GetConn(), 1, win, sn->property, XCB_GET_PROPERTY_TYPE_ANY, 0, 1_MiB / 4);
            xcb_get_property_reply_t *reply = xcb_get_property_reply(X11GetConn(), cookie, nullptr);
            if (reply)
            {
                uint8_t *data = static_cast<uint8_t *>(xcb_get_property_value(reply));
                uint32_t len = xcb_get_property_value_length(reply);
                byteview ret = RegionAlloc::CopyByteView(alloc, byteview{data, len});
                free(reply);
                free(event);
                return ret;
            }
            free(event);
            break;
        }
        // todo: we might need to buffer other events that happened while we waited
        free(event);
    }
    return {};
}

void API Write(byteview text)
{
    xcb_window_t win = X11WinGetHandle();
    if (!win)
        return;

    InlineString::Assign(platform->x11.clipboardContent, text);

    const x11_atoms &atoms = X11GetAtoms();
    xcb_set_selection_owner(X11GetConn(), win, atoms.clipboard, XCB_CURRENT_TIME);
    X11Flush();
}

} // namespace Clipboard

} // namespace nyla
