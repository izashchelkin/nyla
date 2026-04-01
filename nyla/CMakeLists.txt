# set(CMAKE_CXX_CLANG_TIDY "clang-tidy")

add_subdirectory(commons)

add_subdirectory(asset)
add_subdirectory(engine)

add_subdirectory(audio)
add_subdirectory(formats)
add_subdirectory(spirview)
add_subdirectory(tools)

add_subdirectory(apps)

if (UNIX AND NOT APPLE)
    add_subdirectory(dbus)
    add_subdirectory(debugfs)
endif()