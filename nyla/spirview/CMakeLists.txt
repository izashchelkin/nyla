set(TARGET nyla_spirview)
add_library(${TARGET})
add_library(nyla::spirview ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        spirview.cc
    PUBLIC
        spirview.h
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
    PRIVATE
        $ENV{VULKAN_SDK}/Include
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
)




add_executable(${TARGET}_bin)

target_sources(${TARGET}_bin
    PRIVATE
        spirview.cc
        spirview.h
        main.cc
)

target_include_directories(${TARGET}_bin
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(${TARGET}_bin
    PUBLIC
        nyla::commons
)