add_library(nyla_platform)
add_library(nyla::platform ALIAS nyla_platform)

target_sources(nyla_platform
    PRIVATE
        platform.cc
)

target_include_directories(nyla_platform
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(nyla_platform
    PUBLIC
        nyla::commons
        xcb
        xcb-util
        xcb-xkb
        xkbcommon
        xkbcommon-x11
        xcb-xinput
)

#

add_subdirectory(linux)