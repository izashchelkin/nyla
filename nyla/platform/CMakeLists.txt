file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE PLATFORM_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(nyla_platform)
add_library(nyla::platform ALIAS nyla_platform)

target_sources(nyla_platform
    PRIVATE
        ${PLATFORM_SOURCES}
    PUBLIC
        ${PLATFORM_HEADERS}
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