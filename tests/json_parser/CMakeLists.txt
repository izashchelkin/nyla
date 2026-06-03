set(TARGET json_parser_test)

add_executable(${TARGET})
add_test(NAME ${TARGET} COMMAND $<TARGET_FILE:${TARGET}>)

target_link_libraries(${TARGET} PRIVATE nyla_commons)

target_sources(${TARGET} PRIVATE
    test_json_parser.cc
)
