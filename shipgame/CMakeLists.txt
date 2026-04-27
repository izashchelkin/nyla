set(TARGET shipgame)

if (WIN32)
    add_executable(${TARGET} WIN32)
else()
    add_executable(${TARGET})
endif()

target_link_libraries(${TARGET} PRIVATE nyla::commons)

include(CMakeListsGenerated.txt)
