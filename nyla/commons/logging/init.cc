#include "nyla/commons/logging/init.h"

#include "absl/log/globals.h"
#include "absl/log/initialize.h"

namespace nyla
{

void LoggingInit()
{
    static bool init = false;
    if (!init)
    {
        absl::InitializeLog();
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        init = true;
    }
}

} // namespace nyla