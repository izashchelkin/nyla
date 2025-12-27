find_package(Vulkan REQUIRED)

file(GLOB_RECURSE RHI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE RHI_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET nyla_rhi)
add_library(${TARGET})
add_library(nyla::rhi ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${RHI_SOURCES}
    PUBLIC
        ${RHI_HEADERS}
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
        Vulkan::Vulkan        
)

set(TARGET_NDEBUG nyla_rhi_ndebug)
add_library(${TARGET_NDEBUG})
add_library(nyla::rhi_ndebug ALIAS ${TARGET_NDEBUG})

target_sources(${TARGET_NDEBUG}
    PRIVATE
        ${RHI_SOURCES}
    PUBLIC
        ${RHI_HEADERS}
)

target_include_directories(${TARGET_NDEBUG}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NDEBUG}
    PUBLIC
        nyla::commons
        Vulkan::Vulkan
)

target_compile_definitions(${TARGET_NDEBUG}
    PRIVATE
        NDEBUG
)