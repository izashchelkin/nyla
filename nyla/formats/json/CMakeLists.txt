set(TARGET_PARSER nyla_formats_json_parser)
add_library(${TARGET_PARSER})
add_library(nyla::formats::json::parser ALIAS ${TARGET_PARSER})

target_sources(${TARGET_PARSER}
    PRIVATE
        json_parser.cc
        json_visitor.cc
    PUBLIC
        json_parser.h
        json_value.h
)

target_include_directories(${TARGET_PARSER}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PARSER}
    PUBLIC
        nyla::commons::memory
)


set(TARGET_PARSER_TEST nyla_formats_json_parser_test)
add_executable(${TARGET_PARSER_TEST} WIN32)

target_sources(${TARGET_PARSER_TEST}
    PRIVATE
        json_parser_test.cc
)

target_include_directories(${TARGET_PARSER_TEST}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PARSER_TEST}
    PUBLIC
        nyla::formats::json::parser
        nyla::platform
)