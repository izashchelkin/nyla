add_executable(wm)

target_sources(wm
    PRIVATE
        layout.cc
        layout.h
        main.cc
        palette.h
        screen_saver_inhibitor.cc
        screen_saver_inhibitor.h
        window_manager.cc
        window_manager.h
)

target_include_directories(wm
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(wm
    PRIVATE
        nyla::dbus
        nyla::debugfs
        nyla::engine0_ndebug
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
        nyla::engine0
)

