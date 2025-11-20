#include <csignal>

namespace nyla {

#if !defined(IgnoreDebugBreak)
#define DebugBreak raise(SIGTRAP)
#endif

}  // namespace nyla