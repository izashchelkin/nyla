set(TARGET nyla_commons)
add_library(${TARGET})
add_library(nyla::commons ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        color.cc
    INTERFACE
        align.h
        assert.h
        bitenum.h
        byteliterals.h
        byteview.h
        cleanup.h
        color.h
        future.h
        handle.h
        handle_pool.h
        log.h
        string.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

add_subdirectory(memory)
