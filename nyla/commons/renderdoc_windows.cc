#include "nyla/commons/renderdoc.h"

#if !defined(NDEBUG)

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/renderdoc_app.h"

namespace nyla
{

static auto GetRenderDocAPI() -> RENDERDOC_API_1_6_0 *
{
    static RENDERDOC_API_1_6_0 *renderdocApi = nullptr;

    if (renderdocApi)
    {
        return renderdocApi;
    }

    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI renderdocGetApi =
            reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
        if (renderdocGetApi && renderdocGetApi(eRENDERDOC_API_Version_1_6_0, (void **)&renderdocApi) == 1)
        {
            LOG("got renderdoc api");
            return renderdocApi;
        }

        LOG("failed to get renderdoc api");
        TRAP();
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

auto RenderDocTriggerCapture() -> bool
{
    if (auto api = GetRenderDocAPI())
    {
        api->TriggerCapture();
        return true;
    }

    return false;
}

} // namespace nyla

#else

namespace nyla
{

auto RenderDocCaptureStart() -> bool
{
    return false;
}
auto RenderDocCaptureEnd() -> bool
{
    return false;
}
auto RenderDocTriggerCapture() -> bool
{
    return false;
}

} // namespace nyla

#endif
