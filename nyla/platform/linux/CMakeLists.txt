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
add_library(nyla::platform::linux::dir_watch ALIAS ${TARGET_PLATFORM_DIR_WATCH})

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

#

set(target_key_resolver nyla_platform_linux_keyresolver)
add_library(${target_key_resolver})
add_library(nyla::platform::linux::key_resolver ALIAS ${target_key_resolver})

target_sources(${target_key_resolver}
    PRIVATE
        platform_linux_key_resolver.cc
)

target_include_directories(${target_key_resolver}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${target_key_resolver}
    PUBLIC
        nyla::commons
        nyla::platform::linux
)