#include <cstring>
#include <sys/poll.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <unistd.h>

#include "nyla/apps/wm/window_manager.h"
#include "nyla/commons/log.h"
#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"

namespace nyla
{

auto PlatformMain(std::span<const char *> argv) -> int
{
    struct sigaction sa;
    sa.sa_handler = [](int signum) -> void { std::abort(); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    NYLA_ASSERT(sigaction(SIGINT, &sa, nullptr) != -1);

    Platform::Init({
        .enabledFeatures = PlatformFeature::Gfx | PlatformFeature::KeyboardInput | PlatformFeature::MouseInput,
        .open = false,
    });

    WindowManager wm{};
    wm.Init();

    bool isRunning = true;
    while (isRunning && !xcb_connection_has_error(LinuxX11Platform::GetConn()))
    {
        std::array<pollfd, 1> fds{
            pollfd{
                .fd = xcb_get_file_descriptor(LinuxX11Platform::GetConn()),
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
            wm.Process(isRunning);
            LinuxX11Platform::Flush();
        }
    }

    NYLA_LOG("exiting");
    return 0;
}

} // namespace nyla