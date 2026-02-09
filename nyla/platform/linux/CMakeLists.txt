set(TARGET_PLATFORM nyla_platform_linux)
add_library(${TARGET_PLATFORM})
add_library(nyla::platform::linux ALIAS ${TARGET_PLATFORM})

target_sources(${TARGET_PLATFORM}
    PRIVATE
        platform_linux.cc
        platform_linux_page_alloc.cc
        platform_linux_clock.cc
        platform_linux_process.cc
)

target_include_directories(${TARGET_PLATFORM}
    PUBLIC
        ${VULKAN_INCLUDE_DIRS}
)

find_package(Vulkan REQUIRED) # TODO: make this a separate target?

target_link_libraries(${TARGET_PLATFORM}
    PUBLIC
        nyla::commons

        Vulkan::Vulkan

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

set(target_audio nyla_platform_linux_audio)
add_library(${target_audio})
add_library(nyla::platform::linux::audio ALIAS ${target_audio})

target_sources(${target_audio}
    PRIVATE
        platform_linux_audio.cc
)

target_include_directories(${target_audio}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(${target_audio}
    PUBLIC
        nyla::commons
        ${ALSA_LIBRARIES}
)