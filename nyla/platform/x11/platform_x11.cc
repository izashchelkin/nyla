#include "nyla/platform/x11/platform_x11.h"

#include <string>
#include <sys/inotify.h>
#include <unistd.h>

#include <cstdint>
#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/memory/optional.h"
#include "nyla/commons/os/clock.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <xcb/xkb.h>

#undef explicit

namespace nyla
{

using namespace platform_x11_internal;

namespace
{

bool shouldExit = false;

}

void PlatformInit(PlatformInitDesc desc)
{
    X11Initialize(desc.keyboardInput, desc.mouseInput);
}

static X11KeyResolver g_KeyResolver;

void PlatformInputResolverBegin()
{
    g_KeyResolver = {};
    X11InitializeKeyResolver(g_KeyResolver);
}

auto PlatformInputResolve(KeyPhysical key) -> uint32_t
{
    const char *xkbName = ConvertKeyPhysicalIntoXkbName(key);
    const uint32_t keycode = X11ResolveKeyCode(g_KeyResolver, xkbName);
    return keycode;
}

void PlatformInputResolverEnd()
{
    X11FreeKeyResolver(g_KeyResolver);
    g_KeyResolver = {};
}

namespace
{

int fsNotifyFd = 0;

std::array<PlatformFileChanged, 16> fileChanges;
uint32_t fileChangesIndex = 0;
Set<std::string> filesWatched;

} // namespace

void PlatformFsWatchFile(const std::string &path)
{
    if (!fsNotifyFd)
    {
        fsNotifyFd = inotify_init1(IN_NONBLOCK);
        CHECK_GT(fsNotifyFd, 0);
    }

    CHECK_LE(filesWatched.size(), fileChanges.size());

    if (auto [it, ok] = filesWatched.emplace(path); ok)
        inotify_add_watch(fsNotifyFd, path.c_str(), IN_CLOSE);
}

auto PlatformFsGetFileChanges() -> std::span<PlatformFileChanged>
{
    return fileChanges;
}

auto PlatformProcessEvents(const PlatformProcessEventsCallbacks &callbacks, void *user) -> PlatformProcessEventsResult
{
    if (fsNotifyFd)
    {
        char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        char *bufp = buf;

        int numread;
        while ((numread = read(fsNotifyFd, buf, sizeof(buf))) > 0)
        {
            while (bufp != buf + numread)
            {
                const auto *event = reinterpret_cast<inotify_event *>(bufp);
                bufp += sizeof(inotify_event);

                if (event->mask & IN_ISDIR)
                {
                    bufp += event->len;
                    continue;
                }

                std::string path = {bufp, strlen(bufp)};
                bufp += event->len;

                fileChanges[fileChangesIndex] = {
                    .seen = false,
                    .path = path,
                };
                fileChangesIndex = (fileChangesIndex + 1) % fileChanges.size();
            }
        }
    }

    PlatformProcessEventsResult ret{};

    for (;;)
    {
    }

    return ret;
}

auto PlatformShouldExit() -> bool
{
    return shouldExit;
}

namespace platform_x11_internal
{

X11State x11;

auto X11CreateWindow() -> xcb_window_t
{
}

void X11Flush()
{
    xcb_flush(x11.conn);
}

void X11SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                            uint32_t arg4)
{
    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = window,
        .type = type,
        .data = {.data32 = {arg1, arg2, arg3, arg4}},
    };

    xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
}

void X11SendWmTakeFocus(xcb_window_t window, uint32_t time)
{
    X11SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_take_focus, time, 0, 0);
}

void X11SendWmDeleteWindow(xcb_window_t window)
{
    X11SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void X11SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                            uint16_t height, uint16_t borderWidth)
{
    xcb_configure_notify_event_t event = {
        .response_type = XCB_CONFIGURE_NOTIFY,
        .window = window,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .border_width = borderWidth,
    };

    xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char *>(&event));
}

//

auto X11ResolveKeyCode(const X11KeyResolver &keyResolver, std::string_view keyname) -> xcb_keycode_t
{
}

} // namespace platform_x11_internal

} // namespace nyla