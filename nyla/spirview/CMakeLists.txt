set(TARGET nyla_spirview)
add_library(${TARGET})
add_library(nyla::spirview ALIAS ${TARGET})

target_sources(${TARGET}
    PUBLIC
        spirview.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
        $ENV{VULKAN_SDK}/Include
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
)




add_executable(${TARGET}_bin WIN32)

target_sources(${TARGET}_bin
    PRIVATE
        main.cc
)

target_include_directories(${TARGET}_bin
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(${TARGET}_bin
    PUBLIC
        nyla::spirview
        nyla::commons
        nyla::commons::memory
        nyla::platform
)