set(TARGET nyla_alloc)
add_library(${TARGET})
add_library(nyla::alloc LIAS ${TARGET})

target_sources(${TARGET}
    INTERFACE
        region_alloc.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::platform
)


set(REGION_ALLOC_TEST nyla_commons_memory_region_alloc_test)
if (WIN32)
add_executable(${REGION_ALLOC_TEST} WIN32)
else()
add_executable(${REGION_ALLOC_TEST})
endif()

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
        nyla::alloc
        nyla::platform
)

add_test(NAME region_alloc_test COMMAND ${REGION_ALLOC_TEST})