set(TARGET nyla_commons_memory)
add_library(${TARGET})
add_library(nyla::commons::memory ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        region_alloc.cc
    INTERFACE
        region_alloc.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
        nyla::platform
)


set(REGION_ALLOC_TEST nyla_commons_memory_region_alloc_test)
add_executable(${REGION_ALLOC_TEST} WIN32)

target_sources(${REGION_ALLOC_TEST}
    PRIVATE
      region_alloc_test.cc
)


target_include_directories(${REGION_ALLOC_TEST}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${REGION_ALLOC_TEST}
    PUBLIC
        nyla::commons
        nyla::commons::memory
        nyla::platform
)

add_test(NAME region_alloc_test COMMAND ${REGION_ALLOC_TEST})