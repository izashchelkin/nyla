#pragma once

#include <chrono>

namespace nyla
{

auto MakeTimerFd(std::chrono::duration<double> interval) -> int;

}