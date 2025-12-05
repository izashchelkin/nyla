#include "nyla/rhi/renderdoc/renderdoc.h"

#if !defined(NDEBUG)

#include <dlfcn.h>

#include "absl/log/log.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/rhi/renderdoc/renderdoc_app.h"

namespace nyla
{

static auto GetRenderDocAPI() -> RENDERDOC_API_1_6_0 *
{
    static RENDERDOC_API_1_6_0 *renderdoc_api = nullptr;

    if (renderdoc_api)
    {
        return renderdoc_api;
    }

    if (void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
        if (RENDERDOC_GetAPI && RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void **)&renderdoc_api) == 1)
        {
            LOG(INFO) << "got renderdoc api";
            return renderdoc_api;
        }

        LOG(ERROR) << "failed to get renderdoc api";
        DebugBreak;
    }

    return nullptr;
}

auto RenderDocCaptureStart() -> bool
{
    if (auto api = GetRenderDocAPI())
    {
        api->StartFrameCapture(nullptr, nullptr);
        return true;
    }

    return false;
}

auto RenderDocCaptureEnd() -> bool
{
    if (auto api = GetRenderDocAPI())
    {
        api->EndFrameCapture(nullptr, nullptr);
        return true;
    }

    return false;
}

} // namespace nyla

#else

auto RenderDocCaptureStart() -> bool
{
    return false;
}
auto RenderDocCaptureEnd() -> bool
{
    return false;
}

#endif