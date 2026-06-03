set(TARGET coding_agent)

add_executable(${TARGET})
target_link_libraries(${TARGET} PRIVATE nyla::commons_headless xcav_lib)

include(CMakeListsGenerated.txt)
