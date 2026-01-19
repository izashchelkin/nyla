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


set(TARGET_PARSER_TEST nyla_json_parser_test)
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
        nyla::json::parser
        nyla::platform
)

