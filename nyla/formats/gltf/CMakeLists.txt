set(TARGET nyla_formats_gltf)
add_library(${TARGET})
add_library(nyla::formats::gltf ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        gltf.cc
    PUBLIC
        gltf.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::formats::json
)
