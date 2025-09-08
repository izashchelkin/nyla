#include <gtest/gtest.h>

#include <print>

TEST(Layout, basic) {
    //
    std::println("Hello world!");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
