set(TARGET wm_overlay)

if (WIN32)
    add_executable(${TARGET} WIN32)
else()
    add_executable(${TARGET})
endif()

target_link_libraries(${TARGET} PRIVATE nyla::commons)
target_compile_definitions(${TARGET} PRIVATE DLL_IMPORT)

include(CMakeListsGenerated.txt)
