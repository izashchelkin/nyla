#ifndef NYLA_TIMER_H
#define NYLA_TIMER_H

#include <chrono>

namespace nyla {

int MakeTimerFd(std::chrono::duration<double> interval);

}

#endif
