set(TARGET pkgutil)

add_executable(${TARGET})

target_sources(${TARGET} PRIVATE 
    main.cc
)

target_link_libraries(${TARGET}
    PRIVATE
        nyla::platform
        nyla::formats::elf
)