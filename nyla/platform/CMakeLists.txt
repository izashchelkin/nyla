set(target_platform nyla_platform)
add_library(${target_platform} INTERFACE)
add_library(nyla::platform ALIAS ${target_platform})

target_sources(${target_platform}
    INTERFACE
        platform.h
)

set(target_key_resolver nyla_platform_key_resolver)
add_library(${target_key_resolver} INTERFACE)
add_library(nyla::platform::key_resolver ALIAS ${target_key_resolver})

target_sources(${target_key_resolver}
    INTERFACE
        platform_key_resolver.h
)

set(target_dir_watch nyla_platform_dir_watch)
add_library(${target_dir_watch} INTERFACE)
add_library(nyla::platform::dir_watch ALIAS ${target_dir_watch})

target_sources(${target_dir_watch}
    INTERFACE
        platform_dir_watch.h
)

set(target_audio nyla_platform_audio)
add_library(${target_audio} INTERFACE)
add_library(nyla::platform::audio ALIAS ${target_audio})

target_sources(${target_audio}
    INTERFACE
        platform_audio.h
)

if (WIN32)
    add_subdirectory(windows)
    target_link_libraries(${target_platform}
        INTERFACE
            nyla::platform::windows
    )
    target_link_libraries(${target_key_resolver}
        INTERFACE
            nyla::platform::windows::key_resolver
    )
elseif (UNIX AND NOT APPLE)
    add_subdirectory(linux)
    target_link_libraries(${target_platform}
        INTERFACE
            nyla::platform::linux
    )
    target_link_libraries(${target_key_resolver}
        INTERFACE
            nyla::platform::linux::key_resolver
    )
    target_link_libraries(${target_dir_watch}
        INTERFACE
            nyla::platform::linux::dir_watch
    )
    target_link_libraries(${target_audio}
        INTERFACE
            nyla::platform::linux::audio
    )
endif()