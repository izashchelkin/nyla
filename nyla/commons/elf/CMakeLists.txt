set(TARGET nyla_formats_elf)
add_library(${TARGET})
add_library(nyla::formats::elf ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        elf.cc
    PUBLIC
        elf.h
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
)
