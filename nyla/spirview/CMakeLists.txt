add_executable(nyla_spirview_bin WIN32)

target_sources(nyla_spirview_bin
    PRIVATE
        main.cc
)

target_link_libraries(nyla_spirview_bin
    PUBLIC
        nyla::commons
)