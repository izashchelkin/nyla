
#include "nyla/commons/signal/signal.h"

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

}  // namespace nyla