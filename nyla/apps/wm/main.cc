#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <initializer_list>

#include "nyla/apps/wm/window_manager.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/spawn.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/platform.h"
#include "nyla/platform/platform_key_resolver.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla
{

auto PlatformMain() -> int
{
    LoggingInit();
    SigIntCoreDump();
    SigSegvExitZero();

    bool isRunning = true;

    g_Platform->Init({
        .enabledFeatures = PlatformFeature::KeyboardInput | PlatformFeature::MouseInput,
    });
    Platform::Impl *x11 = g_Platform->GetImpl();

    xcb_grab_server(x11->GetConn());

    if (xcb_request_check(x11->GetConn(),
                          xcb_change_window_attributes_checked(
                              x11->GetConn(), x11->GetRoot(), XCB_CW_EVENT_MASK,
                              (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                           XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                           XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                           XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_POINTER_MOTION})))
    {
        NYLA_ASSERT(false && "another wm is already running");
    }

    InitializeWM();

    uint16_t modifier = XCB_MOD_MASK_4;
    std::vector<Keybind> keybinds;

    {
        PlatformKeyResolver keyResolver;
        keyResolver.Init();

        auto spawnTerminal = [] -> void { Spawn({{"ghostty", nullptr}}); };
        auto spawnLauncher = [] -> void { Spawn({{"dmenu_run", nullptr}}); };
        auto nothing = [] -> void {};

        for (auto [key, mod, handler] : std::initializer_list<std::tuple<KeyPhysical, int, KeybindHandler>>{
                 {KeyPhysical::W, 0, NextLayout},
                 {KeyPhysical::E, 0, MoveStackPrev},
                 {KeyPhysical::R, 0, MoveStackNext},
                 {KeyPhysical::E, 1, nothing},
                 {KeyPhysical::R, 1, nothing},
                 {KeyPhysical::D, 0, MoveLocalPrev},
                 {KeyPhysical::F, 0, MoveLocalNext},
                 {KeyPhysical::D, 1, nothing},
                 {KeyPhysical::F, 1, nothing},
                 {KeyPhysical::G, 0, ToggleZoom},
                 {KeyPhysical::X, 0, CloseActive},
                 {KeyPhysical::V, 0, ToggleFollow},
                 {KeyPhysical::T, 0, spawnTerminal},
                 {KeyPhysical::S, 0, spawnLauncher},
             })
        {
            xcb_keycode_t keycode = keyResolver.ResolveKeyCode(key);

            mod |= XCB_MOD_MASK_4;
            keybinds.emplace_back(keycode, mod, handler);

            xcb_grab_key(x11->GetConn(), false, x11->GetRoot(), mod, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        }

        keyResolver.Destroy();
    }

    ManageClientsStartup();

    x11->Flush();
    xcb_ungrab_server(x11->GetConn());

    while (isRunning && !xcb_connection_has_error(x11->GetConn()))
    {
        std::array<pollfd, 1> fds{
            pollfd{
                .fd = xcb_get_file_descriptor(x11->GetConn()),
                .events = POLLIN,
            },
        };
        if (poll(fds.data(), fds.size(), -1) == -1)
        {
            NYLA_LOG("poll(): %s", strerror(errno));
            continue;
        }

        if (fds[0].revents & POLLIN)
        {
            ProcessWMEvents(isRunning, modifier, keybinds);
            ProcessWM();
            x11->Flush();
        }
    }

    NYLA_LOG("exiting");
    return 0;
}

} // namespace nyla