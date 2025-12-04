file(GLOB WM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_executable(wm)

target_sources(wm
    PRIVATE
        ${WM_SOURCES}
)

target_include_directories(wm
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(wm
    PRIVATE
        nyla::dbus
        nyla::debugfs
        nyla::fwk_nvkdbg
        xcb-screensaver
)