set(TARGET_PARSER nyla_formats_elf_parser)
add_executable(${TARGET_PARSER})
add_executable(nyla::formats::elf::parser ALIAS ${TARGET_PARSER})

target_sources(${TARGET_PARSER}
    PRIVATE
        elf_parser.cc
    PUBLIC
        elf_parser.h
)

target_link_libraries(${TARGET_PARSER}
    PUBLIC
        nyla::platform
)

target_include_directories(${TARGET_PARSER}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)