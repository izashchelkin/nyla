set(TARGET nyla_audio_wave)
add_library(${TARGET})
add_library(nyla::audio::wave ALIAS ${TARGET})

target_sources(${TARGET}
    PRIVATE
        wave.cc
)

#

set(TARGET_EXEC nyla_audio_main)
add_executable(${TARGET_EXEC})

target_sources(${TARGET_EXEC}
    PRIVATE
        main.cc
)

target_link_libraries(${TARGET_EXEC}
    PUBLIC
        nyla::platform::audio
        nyla::audio::wave
)