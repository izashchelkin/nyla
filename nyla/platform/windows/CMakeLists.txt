set(TARGET_PLATFORM nyla_platform_windows)
add_library(${TARGET_PLATFORM})
add_library(nyla::platform::windows ALIAS ${TARGET_PLATFORM})

target_sources(${TARGET_PLATFORM}
    PRIVATE
        platform_windows.cc
        platform_windows_clock.cc
        platform_windows_page_alloc.cc
)

target_include_directories(${TARGET_PLATFORM}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PLATFORM}
    PUBLIC
        nyla::commons
        XInput
)