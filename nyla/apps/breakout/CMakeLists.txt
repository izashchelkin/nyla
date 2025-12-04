file(GLOB SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET breakout)
add_executable(${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${SOURCES}
        ${HEADERS}
)

target_include_directories(${TARGET}
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PRIVATE
        nyla::fwk
)