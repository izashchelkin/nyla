#include "nyla/commons/signal/signal.h"

#if defined(__linux__) // TODO: what here?

#include <unistd.h>

#include <csignal>
#include <cstdlib>

#include "absl/log/check.h"

namespace nyla
{

void SigIntCoreDump()
{
    struct sigaction sa;
    sa.sa_handler = [](int signum) -> void { std::abort(); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    CHECK(sigaction(SIGINT, &sa, NULL) != -1);
}

void SigSegvExitZero()
{
    struct sigaction sa;
    sa.sa_handler = [](int signum) -> void {
        usleep(1e9);
        exit(0);
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    CHECK(sigaction(SIGSEGV, &sa, NULL) != -1);
}

} // namespace nyla

#else

void SigIntCoreDump() {}
void SigSegvExitZero() {}

#endif