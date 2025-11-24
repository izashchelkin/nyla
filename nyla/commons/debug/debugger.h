#pragma once

#if !defined(NYLA_NDEBUG) && !defined(NYLA_NDEBUGGER)
#include <csignal>
#define DebugBreak raise(SIGTRAP)
#endif
