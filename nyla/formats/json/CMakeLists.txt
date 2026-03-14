set(TARGET nyla_formats_json)
add_library(${TARGET})
add_library(nyla::formats::json ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        json.cc
        json_value.cc
    PUBLIC
        json.h
        json_value.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)
