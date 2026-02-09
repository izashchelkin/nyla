set(TARGET_PARSER nyla_formats_gltf_parser)
add_library(${TARGET_PARSER})
add_library(nyla::formats::gltf::parser ALIAS ${TARGET_PARSER})

target_sources(${TARGET_PARSER}
    PRIVATE
        gltf_parser.cc
    PUBLIC
        gltf_parser.h
)

target_include_directories(${TARGET_PARSER}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PARSER}
    PUBLIC
        # nyla::commons::memory
        nyla::formats::json::parser
)

set(TARGET_PARSER_TEST nyla_formats_gltf_parser_test)
if (WIN32)
    add_executable(${TARGET_PARSER_TEST} WIN32)
else()
    add_executable(${TARGET_PARSER_TEST})
endif()

target_sources(${TARGET_PARSER_TEST}
    PRIVATE
        gltf_parser_test.cc
)

target_include_directories(${TARGET_PARSER_TEST}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_PARSER_TEST}
    PUBLIC
        nyla::formats::gltf::parser
        nyla::platform
)