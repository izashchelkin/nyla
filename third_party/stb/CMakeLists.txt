set(TARGET stb_image)
add_library(${TARGET})

target_sources(${TARGET}
    PRIVATE
        stb_image.cc
    PUBLIC
        stb_image.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)