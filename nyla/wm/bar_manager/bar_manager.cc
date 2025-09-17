#include "nyla/wm/bar_manager/bar_manager.h"

#include <cstdlib>

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"

namespace nyla {

bool BarManager::Init(xcb_connection_t *conn, xcb_screen_t &screen) {
  return bar_.Init(conn, screen);
}

void BarManager::Update(xcb_connection_t *conn, xcb_screen_t &screen) {
  // std::string_view bar_text;
  //
  // switch (stack_manager.layout_type()) {
  //   case LayoutType::kColumns: {
  //     bar_text = "C";
  //     break;
  //   }
  //   case LayoutType::kRows: {
  //     bar_text = "R";
  //     break;
  //   }
  //   case LayoutType::kGrid: {
  //     bar_text = "G";
  //     break;
  //   }
  // }
  //

  double load_avg[3];
  getloadavg(load_avg, std::size(load_avg));

  bar_.Update(conn, screen,
              absl::StrCat("nylawm [", load_avg[0], ", ", load_avg[1], ", ",
                           load_avg[2], "] ", absl::Now()));
}

}  // namespace nyla
