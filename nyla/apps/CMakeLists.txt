add_subdirectory(breakout)

if (UNIX AND NOT APPLE)
    add_subdirectory(wm)
endif ()

# add_subdirectory(shipgame)
# add_subdirectory(guidemo)
# add_subdirectory(asteroids)