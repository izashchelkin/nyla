set(TARGET_PLATFORM nyla_platform_windows)
add_library(${TARGET_PLATFORM})
add_library(nyla::platform::windows ALIAS ${TARGET_PLATFORM})

target_sources(${TARGET_PLATFORM}
    PRIVATE
        platform_windows.cc
)

target_link_libraries(${TARGET_PLATFORM}
    PRIVATE
        XInput

    PUBLIC
        nyla::commons
        nyla::alloc
)