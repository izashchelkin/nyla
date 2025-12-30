find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)

file(GLOB SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET nyla_dbus)
add_library(${TARGET})
add_library(nyla::dbus ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${SOURCES}
    PUBLIC
        ${HEADERS}
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${DBUS_INCLUDE_DIRS}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
        ${DBUS_LIBRARIES}
)