set (HAS_RHI TARGET nyla::rhi)
if (${HAS_RHI})

    set(TARGET nyla_engine)
    add_library(${TARGET})
    add_library(nyla::engine ALIAS ${TARGET})

    target_sources(${TARGET}
        PRIVATE
            asset_manager.cc
            debug_text_renderer.cc
            engine.cc
            engine0_internal.cc
            glyph_renderer.cc
            gpu_upload_manager.cc
            input_manager.cc
            render_targets.cc
            renderer.cc
            staging.cc
            tween_manager.cc
    )

    target_include_directories(${TARGET}
        PUBLIC
        ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(${TARGET}
        PUBLIC
            nyla::platform
            nyla::platform::dir_watch
            nyla::rhi
            nyla::formats::gltf
            stb_image
    )

endif()