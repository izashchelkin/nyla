#include "nyla/layout/layout.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nyla/layout/rect.h"

namespace nyla {

TEST(Layout, QHD) {
  auto GetScreen = []() {
    static Rect rect{.width = 2560, .height = 1440};
    return rect;
  };

  EXPECT_THAT(ComputeLayout(GetScreen(), 1),
              testing::ElementsAre(GetScreen()));
}

TEST(Layout, UltraWideQHD) {
  auto GetScreen = []() {
    static Rect rect{.width = 3440, .height = 1440};
    return rect;
  };

  EXPECT_THAT(ComputeLayout(GetScreen(), 1),
              testing::ElementsAre(GetScreen()));
}

}  // namespace nyla
