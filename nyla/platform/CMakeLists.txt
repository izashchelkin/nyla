set(target nyla_platform)
add_library(${target} INTERFACE)
add_library(nyla::platform ALIAS ${target})

target_sources(${target}
    PUBLIC
        platform.h
)

set(target_key_resolver nyla_platform_key_resolver)
add_library(${target_key_resolver} INTERFACE)
add_library(nyla::platform::key_resolver ALIAS ${target_key_resolver})

target_sources(${target_key_resolver}
    PUBLIC
        platform_key_resolver.h
)

set(target_dir_watch nyla_platform_dir_watch)
add_library(${target_dir_watch} INTERFACE)
add_library(nyla::platform::dir_watch ALIAS ${target_dir_watch})

target_sources(${target_dir_watch}
    PUBLIC
        platform_dir_watch.h
)

set(target_audio nyla_platform_audio)
add_library(${target_audio} INTERFACE)
add_library(nyla::platform::audio ALIAS ${target_audio})

target_sources(${target_audio}
    PUBLIC
        platform_audio.h
)

if (WIN32)
    add_subdirectory(windows)
elseif (UNIX AND NOT APPLE)
    add_subdirectory(linux)
    target_link_libraries(${target}
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