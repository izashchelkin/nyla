add_executable(nyla_spirview_bin WIN32)

target_sources(nyla_spirview_bin
    PRIVATE
        main.cc
)

target_include_directories(nyla_spirview_bin
    PUBLIC
        ${PROJECT_SOURCE_DIR}  
)

target_link_libraries(nyla_spirview_bin
    PUBLIC
        nyla::platform
)