set(TARGET_PLATFORM nyla_platform_linux)
add_library(${TARGET_PLATFORM})
add_library(nyla::platform::linux ALIAS ${TARGET_PLATFORM})

target_sources(${TARGET_PLATFORM}
    PRIVATE
        platform_linux.cc
)

target_include_directories(${TARGET_PLATFORM}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PLATFORM}
    PUBLIC
        nyla::commons
        nyla::platform

        xcb
        xcb-util
        xcb-xkb
        xkbcommon
        xkbcommon-x11
        xcb-xinput
)

#

set(TARGET_PLATFORM_DIR_WATCH nyla_platform_linux_dir_watch)
add_library(${TARGET_PLATFORM_DIR_WATCH})
add_library(nyla::platform::linux_dir_watch ALIAS ${TARGET_PLATFORM_DIR_WATCH})

target_sources(${TARGET_PLATFORM_DIR_WATCH}
    PRIVATE
        platform_linux_dir_watch.cc
)

target_include_directories(${TARGET_PLATFORM_DIR_WATCH}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PLATFORM_DIR_WATCH}
    PUBLIC
        nyla::commons
)
