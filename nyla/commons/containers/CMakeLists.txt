set(TARGET nyla_commons_containers)
add_library(${TARGET})
add_library(nyla::commons::containers ALIAS ${TARGET})

target_sources(${TARGET}
    INTERFACE
        inline_ring.h
        inline_string.h
        inline_vec.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)