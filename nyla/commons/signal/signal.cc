
#include "nyla/commons/signal/signal.h"

#include <unistd.h>

#include <csignal>

#include "absl/log/check.h"

namespace nyla {

void SigIntCoreDump() {
  struct sigaction sa;
  sa.sa_handler = [](int signum) { CHECK(false); };
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  CHECK(sigaction(SIGINT, &sa, NULL) != -1);
}

void SigSegvExitZero() {
  struct sigaction sa;
  sa.sa_handler = [](int signum) {
    usleep(1e9);
    exit(0);
  };
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  CHECK(sigaction(SIGSEGV, &sa, NULL) != -1);
}

}  // namespace nyla