set(TARGET_PLATFORM nyla_platform_windows)
add_library(${TARGET_PLATFORM})
add_library(nyla::platform::windows ALIAS ${TARGET_PLATFORM})

target_sources(${TARGET_PLATFORM}
    PRIVATE
        platform_windows.cc
)

target_include_directories(${TARGET_PLATFORM}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PLATFORM}
    PUBLIC
        nyla::commons
)

#

set(target_key_resolver nyla_platform_windows_key_resolver)
add_library(${target_key_resolver})
add_library(nyla::platform::windows::key_resolver ALIAS ${target_key_resolver})

target_sources(${target_key_resolver}
    PRIVATE
        platform_windows_key_resolver.cc
)

target_include_directories(${target_key_resolver}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)