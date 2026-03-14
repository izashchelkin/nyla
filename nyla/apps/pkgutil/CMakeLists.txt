set(TARGET pkgutil)

add_executable(${TARGET})

target_sources(${TARGET} PRIVATE 
    main.cc
)

target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR})

target_link_libraries(${TARGET}
    PRIVATE
        nyla::platform
        nyla::formats::elf
)