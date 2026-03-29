set(HAS_RHI TARGET nyla::rhi)

if (${HAS_RHI})

    set(TARGET terminal)

    if (WIN32)
        add_executable(${TARGET} WIN32)
    else()
        add_executable(${TARGET})
    endif()

    target_sources(${TARGET} PRIVATE 
        terminal.cc
        font_atlas.cc
        colortheme.cc
    )

    target_link_libraries(${TARGET}
        PRIVATE
        nyla::engine
        nyla::formats::bdf
    )

endif()