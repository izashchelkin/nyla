#include "nyla/rhi/renderdoc.h"

#if !defined(NDEBUG)

#include <dlfcn.h>

#include "absl/log/log.h"
#include "nyla/commons/debug/debugger.h"
#include "nyla/rhi/renderdoc_app.h"

namespace nyla {

static RENDERDOC_API_1_6_0* GetRenderDocAPI() {
  static RENDERDOC_API_1_6_0* renderdoc_api = nullptr;

  if (renderdoc_api) {
    return renderdoc_api;
  }

  if (void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD)) {
    pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
    if (RENDERDOC_GetAPI && RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&renderdoc_api) == 1) {
      LOG(INFO) << "got renderdoc api";
      return renderdoc_api;
    }

    LOG(ERROR) << "failed to get renderdoc api";
    DebugBreak;
  }

  return nullptr;
}

bool RenderDocCaptureStart() {
  if (auto api = GetRenderDocAPI()) {
    api->StartFrameCapture(nullptr, nullptr);
    return true;
  }

  return false;
}

bool RenderDocCaptureEnd() {
  if (auto api = GetRenderDocAPI()) {
    api->EndFrameCapture(nullptr, nullptr);
    return true;
  }

  return false;
}

}  // namespace nyla

#else

bool RenderDocCaptureStart() {
  return false;
}
bool RenderDocCaptureEnd() {
  return false;
}

#endif