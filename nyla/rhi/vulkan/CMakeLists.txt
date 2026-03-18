find_package(Vulkan)

if (Vulkan_FOUND)
    set(TARGET nyla_rhi_vulkan)
    add_library(${TARGET})
    add_library(nyla::rhi::vulkan ALIAS ${TARGET})

    target_sources(${TARGET}
        PRIVATE
            rhi_vulkan.cc
    )

    target_include_directories(${TARGET}
        PUBLIC
        ${PROJECT_SOURCE_DIR}
    )

    target_link_libraries(${TARGET}
        PUBLIC
            Vulkan::Vulkan        
    )
endif()