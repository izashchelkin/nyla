set(TARGET nyla_commons_math)
add_library(${TARGET})
add_library(nyla::commons::math ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        lerp.cc
    INTERFACE
        lerp.h
        mat.h
        vec.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)