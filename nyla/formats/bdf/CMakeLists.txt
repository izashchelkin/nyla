set(TARGET nyla_formats_bdf)
add_library(${TARGET})
add_library(nyla::formats::bdf ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        bdf.cc
    PUBLIC
        bdf.h
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::platform
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)