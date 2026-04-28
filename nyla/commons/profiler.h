#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace Profiler
{

void API Bootstrap();

void API FrameBegin();
void API FrameEnd();

void API BeginScope(byteview name);
void API EndScope();

void API ToggleVisible();
auto API IsVisible() -> bool;

void API CmdFlush(rhi_cmdlist cmd, int32_t originPxX, int32_t originPxY, uint32_t fps);

} // namespace Profiler

struct profile_scope
{
    profile_scope(byteview name)
    {
        Profiler::BeginScope(name);
    }
    ~profile_scope()
    {
        Profiler::EndScope();
    }
    profile_scope(const profile_scope &) = delete;
    profile_scope &operator=(const profile_scope &) = delete;
};

} // namespace nyla

#define PROFILE_SCOPE_PASTE_2(a, b) a##b
#define PROFILE_SCOPE_PASTE(a, b) PROFILE_SCOPE_PASTE_2(a, b)
#define PROFILE_SCOPE(NAME_LIT)                                                                                        \
    ::nyla::profile_scope PROFILE_SCOPE_PASTE(_ps_, __LINE__)                                                          \
    {                                                                                                                  \
        NAME_LIT##_s                                                                                                   \
    }
