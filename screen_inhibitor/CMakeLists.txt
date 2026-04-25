set(TARGET screen_inhibitor)

if (WIN32)
    add_executable(${TARGET} WIN32)
else()
    add_executable(${TARGET})

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED dbus-1)
endif()


target_link_libraries(${TARGET} PRIVATE nyla::commons ${DBUS_LIBRARIES} xcb-screensaver)
target_include_directories(${TARGET} PRIVATE ${DBUS_INCLUDE_DIRS})

include(CMakeListsGenerated.txt)