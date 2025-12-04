file(GLOB FWK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB FWK_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(nyla_fwk)
add_library(nyla::fwk ALIAS nyla_fwk)

target_sources(nyla_fwk
    PRIVATE
        ${FWK_SOURCES}
    PUBLIC
        ${FWK_HEADERS}
)

target_include_directories(nyla_fwk
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(nyla_fwk
    PUBLIC
        nyla::platform
        nyla::rhi
)

add_library(nyla_fwk_nvkdbg)
add_library(nyla::fwk_nvkdbg ALIAS nyla_fwk_nvkdbg)

target_sources(nyla_fwk_nvkdbg
    PRIVATE
        ${FWK_SOURCES}
    PUBLIC
        ${FWK_HEADERS}
)

target_include_directories(nyla_fwk_nvkdbg
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(nyla_fwk_nvkdbg
    PUBLIC
        nyla::platform
        nyla::rhi_ndbg
)