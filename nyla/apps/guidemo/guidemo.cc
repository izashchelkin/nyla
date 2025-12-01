#include <cstdint>

#include "nyla/commons/logging/init.h"
#include "nyla/commons/memory/temp.h"
#include "nyla/commons/signal/signal.h"
#include "nyla/fwk/dbg_text_renderer.h"
#include "nyla/fwk/gui.h"
#include "nyla/fwk/render_pipeline.h"
#include "nyla/fwk/staging.h"
#include "nyla/platform/platform.h"

namespace nyla {

static uint32_t window;

static int Main() {
  LoggingInit();
  TArenaInit();
  SigIntCoreDump();

  PlatformInit();
  window = PlatformCreateWindow();

  RhiInit(RhiDesc{
      .window = window,
  });

  for (;;) {
    PlatformProcessEvents();
    if (PlatformShouldExit()) {
      break;
    }

    if (RecompileShadersIfNeeded()) {
      RpInit(gui_pipeline);
      RpInit(dbg_text_pipeline);
    }

    RhiFrameBegin();

    static uint32_t fps;
    float dt;
    UpdateDtFps(fps, dt);

    RpBegin(gui_pipeline);
    UI_FrameBegin();

    UI_BoxBegin(50, 50, 200, 120);
    UI_BoxBegin(-50, 50, 200, 120);
    UI_BoxBegin(-50, -50, 200, 120);
    UI_BoxBegin(50, -50, 200, 120);
    UI_Text("Hello world");

    RpBegin(dbg_text_pipeline);
    DbgText(10, 10, "fps= " + std::to_string(fps));

    RhiFrameEnd();
  }

  return 0;
}

}  // namespace nyla

int main() {
  return nyla::Main();
}