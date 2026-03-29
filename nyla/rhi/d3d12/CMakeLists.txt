set(TARGET nyla_rhi_d3d12)
add_library(${TARGET})
add_library(nyla::rhi::d3d12 ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        rhi_d3d12.cc
        rhi_d3d12_swapchain.cc
)

find_package(Vulkan REQUIRED)

target_link_libraries(${TARGET}
    PUBLIC
        d3d12
        dxgi
        dxguid
)