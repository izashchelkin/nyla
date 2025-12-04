find_package(Vulkan REQUIRED)

file(GLOB_RECURSE RHI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE RHI_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(nyla_rhi)
add_library(nyla::rhi ALIAS nyla_rhi)

target_sources(nyla_rhi
    PRIVATE
        ${RHI_SOURCES}
    PUBLIC
        ${RHI_HEADERS}
)

target_include_directories(nyla_rhi
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(nyla_rhi
    PUBLIC
        nyla::commons
        nyla::platform
        Vulkan::Vulkan        
)

add_library(nyla_rhi_ndbg)
add_library(nyla::rhi_ndbg ALIAS nyla_rhi_ndbg)

target_sources(nyla_rhi_ndbg
    PRIVATE
        ${RHI_SOURCES}
    PUBLIC
        ${RHI_HEADERS}
)

target_include_directories(nyla_rhi_ndbg
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(nyla_rhi_ndbg
    PUBLIC
        nyla::commons
        nyla::platform
        Vulkan::Vulkan
)

target_compile_definitions(nyla_rhi_ndbg
    PRIVATE
        NDEBUG
)