set(TARGET nyla_commons)
add_library(${TARGET} SHARED)
add_library(nyla::commons ALIAS ${TARGET})

target_compile_definitions(${TARGET} PRIVATE NYLA_DLL_EXPORT)

target_sources(${TARGET}
    PRIVATE
        fmt.cc
        mem.cc
        path.cc
        region_alloc.cc

    PUBLIC
        align.h
        assert.h
        bitenum.h
        byteliterals.h
        byteparser.h
        byteview.h
        cast.h
        cleanup.h
        color.h
        cstr.h
        fmt.h
        future.h
        handle.h
        handle_pool.h
        hex.h
        inline_ring.h
        inline_string.h
        inline_vec.h
        intrin.h
        lerp.h
        mat.h
        mem.h
        mutstr.h
        path.h
        region_alloc.h
        span.h
        str.h
        vec.h
        word.h

    INTERFACE
        platform.h
        platform_audio.h
        platform_dir_watch.h
)

if(WIN32)
    add_subdirectory(d3d12)
endif()

add_subdirectory(vulkan)

find_package(Vulkan)

if (Vulkan_FOUND)
    target_sources(${TARGET}
        PRIVATE
            vulkan/rhi_vulkan.cc

        PUBLIC
            vulkan/rhi_vulkan.h

        INTERFACE
            rhi.h
    )

    target_link_libraries(${TARGET}
        PUBLIC
            Vulkan::Vulkan        
    )
endif()

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
    target_sources(${TARGET}
        PRIVATE
            windows/platform_windows.cc

        PUBLIC
            windows/platform_windows.h
    )

    target_link_libraries(${TARGET}
        PRIVATE
            XInput
    )

elseif (UNIX AND NOT APPLE)
    # add_subdirectory(linux)
    # target_link_libraries(${target_platform}
    #     INTERFACE
    #         nyla::platform::linux
    # )
    # target_link_libraries(${target_dir_watch}
    #     INTERFACE
    #         nyla::platform::linux::dir_watch
    # )
    # target_link_libraries(${target_audio}
    #     INTERFACE
    #         nyla::platform::linux::audio
    # )
endif()