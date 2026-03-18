if(WIN32)
    add_subdirectory(d3d12)
endif()

add_subdirectory(vulkan)

set(HAS_VULKAN TARGET nyla::rhi::vulkan)
set(HAS_D3D12 TARGET nyla::rhi::d3d12)

if(${HAS_VULKAN} OR ${HAS_D3D12})

    set(TARGET nyla_rhi)
    add_library(${TARGET} INTERFACE)
    add_library(nyla::rhi ALIAS ${TARGET})

    target_sources(${TARGET} INTERFACE
        rhi.h
    )

    # if(${HAS_D3D12})
    #     target_link_libraries(${TARGET} INTERFACE nyla::rhi::d3d12)
    # else()
    target_link_libraries(${TARGET} INTERFACE nyla::rhi::vulkan)
    # endif()

endif()