#include "nyla/commons/os/timerfd.h"

#if defined(__linux__) // TODO: do we still need this?

#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>

namespace nyla
{

auto MakeTimerFd(std::chrono::duration<double> interval) -> int
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd == -1)
        return 0;

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count();
    itimerspec timerSpec = {
        .it_interval = {.tv_nsec = nanos},
        .it_value = {.tv_nsec = nanos},
    };
    if (timerfd_settime(fd, 0, &timerSpec, nullptr) == -1)
    {
        close(fd);
        return 0;
    }

    return fd;
}

} // namespace nyla

#else

namespace nyla
{

auto MakeTimerFd(std::chrono::duration<double> interval) -> int
{
    return -1;
}

} // namespace nyla

#endif