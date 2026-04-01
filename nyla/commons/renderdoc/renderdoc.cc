#include "nyla/rhi/renderdoc/renderdoc.h"

#if !defined(NDEBUG)

#include "nyla/commons/debug/debugger.h"
#include "nyla/rhi/renderdoc/renderdoc_app.h"

namespace nyla
{

static auto GetRenderDocAPI() -> RENDERDOC_API_1_6_0 *
{
    static RENDERDOC_API_1_6_0 *renderdocApi = nullptr;

    if (renderdocApi)
    {
        return renderdocApi;
    }

    if (void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
    {
        pRENDERDOC_GetAPI renderdocGetApi = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
        if (renderdocGetApi && renderdocGetApi(eRENDERDOC_API_Version_1_6_0, (void **)&renderdocApi) == 1)
        {
            NYLA_LOG("got renderdoc api");
            return renderdocApi;
        }

        NYLA_LOG("failed to get renderdoc api");
        DebugBreak();
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