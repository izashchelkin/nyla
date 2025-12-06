file(GLOB FWK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB FWK_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET nyla_engine0)
add_library(${TARGET})
add_library(nyla::engine0 ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${FWK_SOURCES}
    PUBLIC
        ${FWK_HEADERS}
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::platform
        nyla::rhi
)

set(TARGET_NDEBUG nyla_engine0_ndebug)
add_library(${TARGET_NDEBUG})
add_library(nyla::engine0_ndebug ALIAS ${TARGET_NDEBUG})

target_sources(${TARGET_NDEBUG}
    PRIVATE
        ${FWK_SOURCES}
    PUBLIC
        ${FWK_HEADERS}
)

target_include_directories(${TARGET_NDEBUG}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NDEBUG}
    PUBLIC
        nyla::platform
        nyla::rhi_ndebug
)