#include <csignal>

namespace nyla {

#define DebugBreak raise(SIGTRAP)

}  // namespace nyla