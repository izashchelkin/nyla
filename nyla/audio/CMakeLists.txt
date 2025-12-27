find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)

set(TARGET nyla_audio_wave)
add_library(${TARGET})
add_library(nyla::audio::wave ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        wave.cc
)

target_include_directories(${TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        nyla::commons
        nyla::platform
)

#

set(TARGET_EXEC nyla_audio_main)
add_executable(${TARGET_EXEC})

target_sources(${TARGET_EXEC}
    PRIVATE
        main.cc
)

target_include_directories(${TARGET_EXEC}
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_EXEC}
    PUBLIC
        nyla::audio::wave
        nyla::commons
        nyla::platform
        ${ALSA_LIBRARIES}
)
