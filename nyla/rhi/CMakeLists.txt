set(TARGET nyla_rhi)
add_library(${TARGET} INTERFACE)
add_library(nyla::rhi ALIAS ${TARGET})

target_sources(${TARGET}
    INTERFACE
        rhi.h
        rhi_buffer.h
        rhi_cmdlist.h
        rhi_descriptor.h
        rhi_pass.h
        rhi_pipeline.h
        rhi_sampler.h
        rhi_shader.h
        rhi_texture.h
)

add_subdirectory(vulkan)

target_link_libraries(${TARGET}
    INTERFACE
        nyla::rhi::vulkan
)