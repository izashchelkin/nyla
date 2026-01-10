set(TARGET nyla_commons_memory)
add_library(${TARGET})
add_library(nyla::commons::memory ALIAS ${TARGET})

target_sources(${TARGET}
    INTERFACE
        align.h
        byteview.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)