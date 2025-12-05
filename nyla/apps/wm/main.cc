#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <initializer_list>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/apps/wm/window_manager.h"
#include "nyla/apps/wm/wm_background.h"
#include "nyla/commons/future.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/os/spawn.h"
#include "nyla/commons/os/timerfd.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/dbus/dbus.h"
#include "nyla/debugfs/debugfs.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/x11/platform_x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla
{

using namespace platform_x11_internal;

auto Main(int argc, char **argv) -> int
{
    LoggingInit();
    TArenaInit();
    SigIntCoreDump();
    SigSegvExitZero();

    bool is_running = true;

    X11_Initialize();

    xcb_grab_server(x11.conn);

    if (xcb_request_check(x11.conn,
                          xcb_change_window_attributes_checked(
                              x11.conn, x11.screen->root, XCB_CW_EVENT_MASK,
                              (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                           XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                           XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                           XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_POINTER_MOTION})))
    {
        LOG(QFATAL) << "another wm is already running";
    }

    DBus_Initialize();
    DebugFsInitialize(argv[0] + std::string("-debugfs"));
    InitializeWM();

    uint16_t modifier = XCB_MOD_MASK_4;
    std::vector<Keybind> keybinds;

    {
        X11_KeyResolver key_resolver;
        X11_InitializeKeyResolver(key_resolver);

        auto SpawnTerminal = [] -> void { Spawn({{"ghostty", nullptr}}); };
        auto SpawnLauncher = [] -> void { Spawn({{"dmenu_run", nullptr}}); };
        auto Nothing = [] -> void {};

        for (auto [key, mod, handler] : std::initializer_list<std::tuple<KeyPhysical, int, KeybindHandler>>{
                 {KeyPhysical::W, 0, NextLayout},
                 {KeyPhysical::E, 0, MoveStackPrev},
                 {KeyPhysical::R, 0, MoveStackNext},
                 {KeyPhysical::E, 1, Nothing},
                 {KeyPhysical::R, 1, Nothing},
                 {KeyPhysical::D, 0, MoveLocalPrev},
                 {KeyPhysical::F, 0, MoveLocalNext},
                 {KeyPhysical::D, 1, Nothing},
                 {KeyPhysical::F, 1, Nothing},
                 {KeyPhysical::G, 0, ToggleZoom},
                 {KeyPhysical::X, 0, CloseActive},
                 {KeyPhysical::V, 0, ToggleFollow},
                 {KeyPhysical::T, 0, SpawnTerminal},
                 {KeyPhysical::S, 0, SpawnLauncher},
             })
        {
            const char *xkb_name = ConvertKeyPhysicalIntoXkbName(key);
            xcb_keycode_t keycode = X11_ResolveKeyCode(key_resolver, xkb_name);

            mod |= XCB_MOD_MASK_4;
            keybinds.emplace_back(keycode, mod, handler);

            xcb_grab_key(x11.conn, false, x11.screen->root, mod, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        }

        X11_FreeKeyResolver(key_resolver);
    }

    using namespace std::chrono_literals;
    int tfd = MakeTimerFd(501ms);

    ManageClientsStartup();

    std::vector<pollfd> fds;
    fds.emplace_back(pollfd{
        .fd = xcb_get_file_descriptor(x11.conn),
        .events = POLLIN,
    });
    fds.emplace_back(pollfd{
        .fd = debugfs.fd,
        .events = POLLIN,
    });

    if (!tfd)
        LOG(QFATAL) << "MakeTimerFdMillis";
    absl::Cleanup tfd_closer = [tfd] -> void { close(tfd); };
    fds.emplace_back(pollfd{
        .fd = tfd,
        .events = POLLIN,
    });

    DebugFsRegister(
        "quit", &is_running, //
        [](auto &file) -> auto { file.content = "quit\n"; },
        [](auto &file) -> auto {
            *reinterpret_cast<bool *>(file.data) = false;
            LOG(INFO) << "exit requested";
        });

    xcb_flush(x11.conn);
    xcb_ungrab_server(x11.conn);

    //

    {
        std::future<void> fut = InitWMBackground();
        while (!IsFutureReady(fut) && is_running && !xcb_connection_has_error(x11.conn))
        {
            if (poll(fds.data(), fds.size(), -1) == -1)
            {
                continue;
            }

            if (fds[0].revents & POLLIN)
            {
                ProcessWMEvents(is_running, modifier, keybinds);
                ProcessWM();
                xcb_flush(x11.conn);
            }
        }
    }

    {
        while (is_running && !xcb_connection_has_error(x11.conn))
        {
            if (poll(fds.data(), fds.size(), -1) == -1)
            {
                continue;
            }

            if (fds[0].revents & POLLIN)
            {
                ProcessWMEvents(is_running, modifier, keybinds);
                ProcessWM();
                xcb_flush(x11.conn);
            }

            if (fds[1].revents & POLLIN)
            {
                DebugFsProcess();
            }

            if (fds[2].revents & POLLIN)
            {
                uint64_t expirations = 0;
                if (read(tfd, &expirations, sizeof(expirations)) > 0 && expirations > 0)
                {
                    wm_background_dirty = true;
                }
            }

            if (wm_background_dirty)
            {
                UpdateBackground();
            }

            DBus_Process();
        }
    }

    //

    return 0;
}

} // namespace nyla

auto main(int argc, char **argv) -> int
{
    return nyla::Main(argc, argv);
}