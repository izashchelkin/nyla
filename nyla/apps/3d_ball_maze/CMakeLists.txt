set (HAS_ENGINE TARGET nyla::engine)
if (${HAS_ENGINE})

    set(TARGET 3d_ball_maze)

    if (WIN32)
        add_executable(${TARGET} WIN32)
    else()
        add_executable(${TARGET})
    endif()

    target_sources(${TARGET} PRIVATE 
        3d_ball_maze.cc
        game_scene.cc
        level_editor_scene.cc
    )

    target_link_libraries(${TARGET} PRIVATE nyla::engine)

    if (WIN32)
        target_link_libraries(${TARGET} PRIVATE user32)
    endif()

endif()