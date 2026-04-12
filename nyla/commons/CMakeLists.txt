set(TARGET nyla_commons)
add_library(${TARGET} SHARED)
add_library(nyla::commons ALIAS ${TARGET})

target_compile_definitions(${TARGET} PRIVATE NYLA_DLL_EXPORT)

target_sources(${TARGET}
    PRIVATE
        asset_manager.cc
        bdf.cc
        bootstrap.cc
        byteparser.cc
        debug_text_renderer.cc
        engine.cc
        engine0_internal.cc
        fmt.cc
        gltf.cc
        glyph_renderer.cc
        gpu_upload_manager.cc
        input_manager.cc
        json_parser.cc
        json_value.cc
        mem.cc
        png.cc
        region_alloc.cc
        render_targets.cc
        renderer.cc
        tween_manager.cc

    PUBLIC
        align.h
        array.h
        assert.h
        asset_manager.h
        bdf.h
        bitenum.h
        bootstrap.h
        byteliterals.h
        byteparser.h
        cast.h
        cleanup.h
        collections.h
        color.h
        complex.h
        concepts.h
        cstr.h
        debug_text_renderer.h
        engine.h
        engine0_internal.h
        entrypoint.cc
        fmt.h
        fmt_string.h
        gltf.h
        glyph_renderer.h
        gpu_upload_manager.h
        handle.h
        handle_pool.h
        hex.h
        inline_path.h
        inline_queue.h
        inline_vec.h
        input_manager.h
        intrin.h
        json_parser.h
        json_value.h
        lerp.h
        limits.h
        log.h
        macros.h
        mat.h
        math.h
        mem.h
        minmax.h
        platform.h
        platform_audio.h
        platform_base.h
        platform_dir_watch.h
        png.h
        region_alloc.h
        render_targets.h
        renderer.h
        rhi.h
        span.h
        spirview.h
        staging.h
        staging_buffer.h
        str.h
        tuple.h
        tween_manager.h
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

add_subdirectory(elf)