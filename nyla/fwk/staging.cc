#include "absl/log/log.h"
#include "nyla/platform/platform.h"

namespace nyla {

bool RecompileShadersIfNeeded() {
  static bool spv_changed = true;
  static bool src_changed = true;

  for (auto& change : PlatformFsGetChanges()) {
    if (change.seen) continue;
    change.seen = true;

    const auto& path = change.path;
    if (path.ends_with(".spv")) {
      spv_changed = true;
    } else if (path.ends_with(".vert") || path.ends_with(".frag")) {
      src_changed = true;
    }
  }

  if (src_changed) {
    LOG(INFO) << "shaders recompiling";
    system("python3 /home/izashchelkin/nyla/scripts/shaders.py");
    usleep(1e6);
    PlatformProcessEvents();

    src_changed = false;
  }

  if (src_changed || spv_changed) {
    spv_changed = false;
    return true;
  }

  return false;
}

}  // namespace nyla