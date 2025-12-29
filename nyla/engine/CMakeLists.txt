set(TARGET nyla_engine)
add_library(${TARGET})
add_library(nyla::engine ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        asset_manager.cc
        debug_text_renderer.cc
        engine.cc
        engine0_internal.cc
        frame_arena.cc
        renderer2d.cc
        staging.cc
        staging_buffer.cc
        tween_manager.cc
        input_manager.cc
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::platform
        nyla::platform::key_resolver
        nyla::platform::dir_watch
        nyla::rhi
        nyla::spirview
        stb_image
)

set(TARGET_DEBUG_TEXT_RENDERER nyla_debug_text_renderer)
add_library(${TARGET_DEBUG_TEXT_RENDERER})
add_library(nyla::debug_text_renderer ALIAS ${TARGET_DEBUG_TEXT_RENDERER})

target_sources(${TARGET_DEBUG_TEXT_RENDERER}
    PRIVATE
        engine0_internal.cc
        debug_text_renderer.cc
)

target_include_directories(${TARGET_DEBUG_TEXT_RENDERER}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_DEBUG_TEXT_RENDERER}
    PUBLIC
        nyla::spirview
)