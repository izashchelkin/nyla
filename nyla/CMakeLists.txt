# set(CMAKE_CXX_CLANG_TIDY "clang-tidy")

add_subdirectory(commons)
add_subdirectory(tools)

if (UNIX AND NOT APPLE)
    add_subdirectory(dbus)
endif()