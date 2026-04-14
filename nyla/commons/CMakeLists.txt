set(TARGET nyla_commons)
add_library(${TARGET} SHARED)
add_library(nyla::commons ALIAS ${TARGET})

target_compile_definitions(${TARGET} PRIVATE NYLA_DLL_EXPORT)

find_package(Vulkan)

target_sources (${TARGET} PRIVATE
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
    mempage_pool.cc
    png.cc
    region_alloc.cc
    render_targets.cc
    renderer.cc
    shader.cc
    spv_shader.cc
    staging_buffer.cc
    staging.cc
    tween_manager.cc
)

target_sources (${TARGET} PUBLIC
    align.h
    array_def.h
    array.h
    asset_manager.h
    bdf.h
    bitenum.h
    bootstrap.h
    byteliterals.h
    byteparser.h
    cast.h
    collections.h
    color.h
    complex.h
    concepts.h
    debug_text_renderer.h
    elf.h
    engine.h
    engine0_internal.h
    entrypoint.h
    file_utils.h
    file.h
    fmt.h
    gamepad.h
    gltf.h
    glyph_renderer.h
    gpu_upload_manager.h
    handle_pool.h
    handle.h
    hex.h
    inline_path.h
    inline_queue.h
    inline_string.h
    inline_vec_def.h
    inline_vec.h
    input_manager.h
    intrin.h
    json_parser.h
    json_value.h
    keyboard.h
    lerp.h
    limits.h
    linux_abi.h
    macros.h
    mat.h
    math.h
    mem.h
    mempage_pool.h
    minmax.h
    platform_audio.h
    platform_dir_watch.h
    platform.h
    png.h
    region_alloc_def.h
    region_alloc.h
    render_targets.h
    renderer.h
    rhi.h
    shader.h
    span_def.h
    span.h
    spv_reader.h
    spv_shader.h
    staging_buffer.h
    staging.h
    time.h
    tuple.h
    tween_manager.h
    vec.h
    word.h
)

if (WIN32)
    target_sources (${TARGET} PRIVATE
        platform_windows.cc
    )
    target_sources (${TARGET} PUBLIC
        entrypoint_windows.h
        platform_windows.h
    )
else()
    target_sources (${TARGET} PRIVATE
        platform_linux.cc
    )
    target_sources (${TARGET} PUBLIC
        entrypoint_linux.h
        platform_linux.h
    )
endif()

if (Vulkan_FOUND)
    target_sources (${TARGET} PRIVATE
        rhi_vulkan.cc
    )
endif()

if (Vulkan_FOUND)
    target_sources(${TARGET}
        PRIVATE
            rhi_vulkan.cc

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
            platform_windows.cc

        PUBLIC
            platform_windows.h
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