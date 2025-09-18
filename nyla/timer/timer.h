#pragma once

#include <chrono>

namespace nyla {

int MakeTimerFd(std::chrono::duration<double> interval);

}
