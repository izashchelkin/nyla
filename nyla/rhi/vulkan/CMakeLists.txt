set(TARGET nyla_rhi_vulkan)
add_library(${TARGET})
add_library(nyla::rhi::vulkan ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        rhi_vulkan.cc
        rhi_vulkan_buffer.cc
        rhi_vulkan_command_list.cc
        rhi_vulkan_descriptor.cc
        rhi_vulkan_frame.cc
        rhi_vulkan_pass.cc
        rhi_vulkan_pipeline.cc
        rhi_vulkan_sampler.cc
        rhi_vulkan_swapchain.cc
        rhi_vulkan_sync.cc
        rhi_vulkan_texture.cc
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

find_package(Vulkan REQUIRED)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
        Vulkan::Vulkan        
)