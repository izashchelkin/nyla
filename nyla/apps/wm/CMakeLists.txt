add_executable(wm)

target_sources(wm
    PRIVATE
        layout.cc
        layout.h
        main.cc
        palette.h
        window_manager.cc
        window_manager.h
)

target_include_directories(wm
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(wm
    PRIVATE
        nyla::platform::linux
        nyla::platform::linux::key_resolver
        xcb-screensaver
)

add_executable(wm_overlay)

target_sources(wm_overlay
    PRIVATE
        wm_overlay.cc
        wm_overlay.h
)

target_link_libraries(wm_overlay
    PRIVATE
        nyla::debug_text_renderer
        nyla::rhi
        nyla::platform::linux
)