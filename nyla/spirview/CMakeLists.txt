file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET nyla_spirview)
add_library(${TARGET})
add_library(nyla::spirview ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${SOURCES}
    PUBLIC
        ${HEADERS}
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
)



# TEMPORARY!
add_executable(${TARGET}_bin)

target_sources(${TARGET}_bin
    PRIVATE
        ${SOURCES}
        ${HEADERS}
)

target_include_directories(${TARGET}_bin
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(${TARGET}_bin
    PUBLIC
        nyla::commons
)

# TEMPORARY!
