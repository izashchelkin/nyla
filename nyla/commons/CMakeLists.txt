set(TARGET nyla_commons)
add_library(${TARGET})
add_library(nyla::commons ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        path.cc
        mem.cc
    PUBLIC
        align.h
        assert.h
        bitenum.h
        byteliterals.h
        byteparser.h
        byteview.h
        cast.h
        cleanup.h
        color.h
        future.h
        handle.h
        handle_pool.h
        hex.h
        inline_ring.h
        inline_string.h
        inline_vec.h
        lerp.h
        log.h
        mat.h
        path.h
        mem.h
        vec.h
        word.h
)
