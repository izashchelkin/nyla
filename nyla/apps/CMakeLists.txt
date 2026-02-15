add_subdirectory(breakout)
add_subdirectory(3d_ball_maze)

if (UNIX AND NOT APPLE)
    add_subdirectory(wm)
endif ()

# add_subdirectory(shipgame)
# add_subdirectory(guidemo)
# add_subdirectory(asteroids)