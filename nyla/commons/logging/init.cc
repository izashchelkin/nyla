#include "nyla/commons/logging/init.h"

#include "absl/log/globals.h"
#include "absl/log/initialize.h"

namespace nyla {

void InitLogging() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
}

}  // namespace nyla
