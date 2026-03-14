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
    )

    target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR})

    target_link_libraries(${TARGET}
        PRIVATE
        nyla::platform
        nyla::rhi
    )

endif()