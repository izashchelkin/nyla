if (UNIX AND NOT APPLE)

    add_executable(wm)

    target_sources(wm
        PRIVATE
        layout.cc
        main.cc
        window_manager.cc
    )

    target_include_directories(wm
        PRIVATE
        ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(wm
        PRIVATE
        nyla::platform::linux
        xcb-screensaver
    )


    set (HAS_RHI TARGET nyla::rhi)

    if (${HAS_RHI})

        add_executable(wm_overlay)

        target_sources(wm_overlay
            PRIVATE
            wm_overlay.cc
            wm_overlay.h
        )

        target_link_libraries(wm_overlay
            PRIVATE
            nyla::engine
        )

    endif()

endif()