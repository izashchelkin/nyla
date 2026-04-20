#include "nyla/commons/platform_linux.h"
#include "nyla/commons/file.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <linux/close_range.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xinput.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

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

#include "nyla/commons/fmt.h"
#include "nyla/commons/span.h"

namespace nyla
{

platform_state *platform;

//

void API Sleep(uint64_t millis)
{
    usleep(millis * 1000L);
}

//

auto API FileValid(file_handle file) -> bool
{
    int fd = (int)(int64_t)file;
    return fd >= 0;
}

auto API FileOpen(byteview path, FileOpenMode mode) -> file_handle
{
    int flag = 0;

    if (mode == FileOpenMode::Read)
        flag = O_RDONLY;
    else
    {
        const bool append = Any(mode & FileOpenMode::Append);
        if (append)
            flag |= O_APPEND | O_CREAT;
        const bool write = Any(mode & FileOpenMode::Write);
        if (write)
            flag |= O_CREAT | O_TRUNC;

        if (append || write)
        {
            if (Any(mode & FileOpenMode::Read))
                flag |= O_RDWR;
            else
                flag |= O_WRONLY;
        }
    }

    return (void *)(int64_t)open(Span::CStr(path), flag, 0666);
}

void API FileClose(file_handle file)
{
    int fd = (int)(int64_t)file;
    close(fd);
}

auto API FileRead(file_handle file, uint32_t size, uint8_t *out) -> uint32_t
{
    int fd = (int)(int64_t)file;
    ssize_t ret = read(fd, out, size);
    ASSERT(ret >= 0);
    return (uint32_t)ret;
}

auto API FileWrite(file_handle file, uint32_t size, const uint8_t *in) -> uint32_t
{
    int fd = (int)(int64_t)file;
    ssize_t ret = write(fd, in, size);
    ASSERT(ret >= 0);
    return (uint32_t)ret;
}

void API FileSeek(file_handle file, int64_t at, file_seek_mode mode)
{
    int fd = (int)(int64_t)file;
    int whence;

    switch (mode)
    {
    case file_seek_mode::Current:
        whence = SEEK_CUR;
        break;
    case file_seek_mode::Begin:
        whence = SEEK_SET;
        break;
    case file_seek_mode::End:
        whence = SEEK_END;
        break;
    }

    ASSERT(lseek(fd, at, whence) >= 0);
}

auto API FileTell(file_handle file) -> uint64_t
{
    int fd = (int)(int64_t)file;
    __off_t ret = lseek(fd, 0, SEEK_CUR);
    ASSERT(ret >= 0);
    return ret;
}

auto API GetStdin() -> file_handle
{
    return (void *)0;
}

auto API GetStdout() -> file_handle
{
    return (void *)1;
}

auto API GetStderr() -> file_handle
{
    return (void *)2;
}

//

auto API UpdateGamepad(uint32_t index) -> bool
{
    return false;
}

auto API GetGamepadLeftStick(uint32_t index) -> float2
{
    return {};
}

auto API GetGamepadRightStick(uint32_t index) -> float2
{
    return {};
}

auto API GetGamepadLeftTrigger(uint32_t index) -> float
{
    return 0.f;
}

auto API GetGamepadRightTrigger(uint32_t index) -> float
{
    return 0.f;
}

auto API GetMonotonicTimeMillis() -> uint64_t
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000 + ts.tv_nsec / 1'000'000;
}

auto API GetMonotonicTimeMicros() -> uint64_t
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000'000 + ts.tv_nsec / 1'000;
}

auto API GetMonotonicTimeNanos() -> uint64_t
{
    timespec ts{};
    ASSERT(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0);
    return ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
}

auto API ReserveMemPages(uint64_t size) -> void *
{
    void *p = (char *)mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(p != MAP_FAILED);
    return p;
}

void API CommitMemPages(void *page, uint64_t size)
{
    mprotect(page, size, PROT_READ | PROT_WRITE);
}

void API DecommitMemPages(void *page, uint64_t size)
{
    mprotect(page, size, PROT_NONE);
    madvise(page, size, MADV_DONTNEED);
}

auto API WinGetSize() -> PlatformWindowSize
{
    return PlatformWindowSize{
        .width = platform->x11.winGeom.width,
        .height = platform->x11.winGeom.height,
    };
}

void API WinOpen()
{
    if (X11WinGetHandle())
    {
        xcb_map_window(X11GetConn(), X11WinGetHandle());
        return;
    }

    constexpr auto kEventMask = static_cast<xcb_event_mask_t>(
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY);
    platform->x11.win =
        X11CreateWin(X11GetScreen()->width_in_pixels, X11GetScreen()->height_in_pixels, false, kEventMask);

    xcb_get_geometry_reply_t *windowGeometry =
        xcb_get_geometry_reply(X11GetConn(), xcb_get_geometry(X11GetConn(), X11WinGetHandle()), nullptr);
    platform->x11.winGeom = *windowGeometry;
    free(windowGeometry);
}

namespace
{

INLINE auto ProcessXEvent(xcb_generic_event_t *event, PlatformEvent &outEvent) -> bool
{
    const uint8_t eventType = event->response_type & 0x7F;

    switch (eventType)
    {
    case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t *>(event);

        for (uint32_t i = 1; i < static_cast<uint32_t>(KeyPhysical::Count); ++i)
        {
            if (platform->x11.keyCodes[i] == keypress->detail)
            {
                outEvent = PlatformEvent{
                    .type = PlatformEventType::KeyDown,
                    .key = static_cast<KeyPhysical>(i),
                };
                return true;
            }
        }

        return false;
    }

    case XCB_KEY_RELEASE: {
        auto keyrelease = reinterpret_cast<xcb_key_release_event_t *>(event);

        for (uint32_t i = 1; i < static_cast<uint32_t>(KeyPhysical::Count); ++i)
        {
            if (platform->x11.keyCodes[i] == keyrelease->detail)
            {
                outEvent = PlatformEvent{
                    .type = PlatformEventType::KeyUp,
                    .key = static_cast<KeyPhysical>(i),
                };
                return true;
            }
        }

        return false;
    }

    case XCB_BUTTON_PRESS: {
        auto buttonpress = reinterpret_cast<xcb_button_press_event_t *>(event);
        outEvent = PlatformEvent{
            .type = PlatformEventType::MousePress,
            .mouse = {.code = buttonpress->detail},
        };
        return true;
    }

    case XCB_BUTTON_RELEASE: {
        auto buttonrelease = reinterpret_cast<xcb_button_release_event_t *>(event);
        outEvent = PlatformEvent{
            .type = PlatformEventType::MouseRelease,
            .mouse = {.code = buttonrelease->detail},
        };
        return true;
    }

    case XCB_CONFIGURE_NOTIFY: {
        auto configurenotify = reinterpret_cast<xcb_configure_notify_event_t *>(event);

        xcb_get_geometry_reply_t *newGeom =
            xcb_get_geometry_reply(X11GetConn(), xcb_get_geometry(X11GetConn(), X11WinGetHandle()), nullptr);

        const bool windowResized = newGeom->width != WinGetSize().width || newGeom->height != WinGetSize().height;
        platform->x11.winGeom = *newGeom;
        free(newGeom);

        if (windowResized)
        {
            outEvent = PlatformEvent{.type = PlatformEventType::WinResize};
            return true;
        }

        return false;
    }

    case XCB_EXPOSE: {
        outEvent = PlatformEvent{.type = PlatformEventType::Repaint};
        return true;
    }

    case XCB_CLIENT_MESSAGE: {
        auto clientmessage = reinterpret_cast<xcb_client_message_event_t *>(event);

        if (clientmessage->format == 32 && clientmessage->type == X11GetAtoms().wm_protocols &&
            clientmessage->data.data32[0] == X11GetAtoms().wm_delete_window)
        {
            outEvent = {.type = PlatformEventType::Quit};
            return true;
        }

        return false;
    }

    default:
        return false;
    }
}

} // namespace

auto API WinPollEvent(PlatformEvent &outEvent) -> bool
{
    for (;;)
    {
        if (xcb_connection_has_error(X11GetConn()))
        {
            outEvent = {.type = PlatformEventType::Quit};
            return true;
        }

        xcb_generic_event_t *event = xcb_poll_for_event(X11GetConn());
        if (!event)
            return false;

        bool done = ProcessXEvent(event, outEvent);
        free(event);

        if (done)
            return true;
    }
}

auto API Spawn(span<const char *const> cmd) -> bool
{
    { // TODO: move this somewhere
        static bool installed = false;
        if (!installed)
        {
            struct sigaction sa;
            sa.sa_handler = [](int signum) -> void {
                pid_t pid;
                int status;
                while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
                    ;
            };
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;
            if (sigaction(SIGCHLD, &sa, nullptr) == -1)
            {
                LOG("sigaction failed");
                return false;
            }
            else
            {
                installed = true;
            }
        }
    }

    if (cmd.size <= 1)
        return false;
    if (Span::Back(cmd) != nullptr)
        return false;

    switch (fork())
    {
    case -1:
        return false;
    case 0:
        break;
    default:
        return true;
    }

    int devNullFd = open("/dev/null", O_RDWR);
    if (devNullFd != -1)
    {
        dup2(devNullFd, STDIN_FILENO);
        dup2(devNullFd, STDOUT_FILENO);
        dup2(devNullFd, STDERR_FILENO);
    }

    if (close_range(3, ~0U, CLOSE_RANGE_UNSHARE) != 0)
    {
        goto failure;
    }

    execvp(cmd[0], const_cast<char *const *>(cmd.data));

failure:
    _exit(127);
}

//

void PlatformBootstrap()
{
    ASSERT(sysconf(_SC_PAGESIZE) == kPageSize);

    {
        platform->x11.conn = xcb_connect(nullptr, &platform->x11.screenIndex);
        if (xcb_connection_has_error(X11GetConn()))
            ASSERT(false && "could not connect to X server");

        platform->x11.screen = xcb_aux_get_screen(X11GetConn(), X11GetScreenIndex());
        ASSERT(X11GetScreen());

#define X(varname, atomname) platform->x11.atoms.varname = X11InternAtom(#atomname##_s, false);
        NYLA_X11_ATOMS(X)
#undef X
    }

    {
        uint16_t majorXkbVersionOut, minorXkbVersionOut;
        uint8_t baseEventOut, baseErrorOut;

        if (!xkb_x11_setup_xkb_extension(X11GetConn(), XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                         XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &majorXkbVersionOut, &minorXkbVersionOut,
                                         &baseEventOut, &baseErrorOut))
            ASSERT(false && "could not set up xkb extension");

        xcb_generic_error_t *err = nullptr;
        xcb_xkb_per_client_flags_reply_t *reply = xcb_xkb_per_client_flags_reply(
            X11GetConn(),
            xcb_xkb_per_client_flags(X11GetConn(), XCB_XKB_ID_USE_CORE_KBD,
                                     XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                     XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0),
            &err);
        if (!reply || err)
            ASSERT(false && "could not set up detectable autorepeat");
    }

    {
        xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        ASSERT(ctx);

        const int32_t deviceId = xkb_x11_get_core_keyboard_device_id(X11GetConn());
        ASSERT(deviceId != -1);

        xkb_keymap *keymap = xkb_x11_keymap_new_from_device(ctx, X11GetConn(), deviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
        ASSERT(keymap);

        for (uint32_t i = 1; i < static_cast<uint32_t>(KeyPhysical::Count); ++i)
        {
            const auto key = static_cast<KeyPhysical>(i);
            byteview xkbName = X11ConvertKeyPhysicalIntoXkbName(key);
            const xkb_keycode_t keycode = xkb_keymap_key_by_name(keymap, Span::CStr(xkbName));
            ASSERT(xkb_keycode_is_legal_x11(keycode));
            platform->x11.keyCodes[i] = keycode;
        }

        xkb_keymap_unref(keymap);
        xkb_context_unref(ctx);
    }

    {
        const xcb_query_extension_reply_t *ext = xcb_get_extension_data(X11GetConn(), &xcb_input_id);
        if (!ext || !ext->present)
            ASSERT(false && "could nolt set up XI2 extension");

        struct
        {
            xcb_input_event_mask_t eventMask;
            uint32_t maskBits;
        } mask;

        mask.eventMask.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
        mask.eventMask.mask_len = 2;
        mask.maskBits = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION | XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_PRESS;

        if (xcb_request_check(X11GetConn(),
                              xcb_input_xi_select_events_checked(X11GetConn(), X11GetRoot(), 1, &mask.eventMask)))
            ASSERT(false && "could not setup XI2 extension");

        platform->x11.extensionXInput2MajorOpCode = ext->major_opcode;
    }
}

void PlatformTearDown()
{
}

} // namespace nyla