set(TARGET_PARSER nyla_json_parser)
add_library(${TARGET_PARSER})
add_library(nyla::json::parser ALIAS ${TARGET_PARSER})

target_sources(${TARGET_PARSER}
    PRIVATE
        json_parser.cc
    PUBLIC
        json_parser.h
)

target_include_directories(${TARGET_PARSER}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PARSER}
    PUBLIC
        nyla::commons::memory
)