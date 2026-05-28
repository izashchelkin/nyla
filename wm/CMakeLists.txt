if (NOT WIN32)
    set(TARGET wm)

    add_executable(${TARGET})

    target_link_libraries(${TARGET} PRIVATE nyla::commons)

    include(CMakeListsGenerated.txt)
endif()
