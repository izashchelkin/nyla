file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET nyla_commons)
add_library(${TARGET})
add_library(nyla::commons ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${SOURCES}
    PUBLIC
        ${HEADERS}
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)