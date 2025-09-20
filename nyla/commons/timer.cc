#include "nyla/commons/timer.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>

namespace nyla {

int MakeTimerFd(std::chrono::duration<double> interval) {
  int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd == -1) return 0;

  auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count();
  itimerspec timer_spec = {.it_interval = {.tv_nsec = nanos},
                           .it_value = {.tv_nsec = nanos}};
  if (timerfd_settime(fd, 0, &timer_spec, nullptr) == -1) {
    close(fd);
    return 0;
  }

  return fd;
}

}  // namespace nyla
