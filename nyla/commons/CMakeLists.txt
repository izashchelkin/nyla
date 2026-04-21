set(TARGET nyla_commons)
add_library(${TARGET} SHARED)
add_library(nyla::commons ALIAS ${TARGET})

target_compile_definitions(${TARGET} PRIVATE DLL_EXPORT)

find_package(Vulkan)

if (Vulkan_FOUND)
    target_link_libraries(${TARGET}
        PUBLIC
            Vulkan::Vulkan        
    )
endif()

if (WIN32)
    target_link_libraries(${TARGET}
        PRIVATE
            XInput
    )
else()

target_link_libraries(${TARGET}
    PUBLIC
        xcb
        xcb-errors
        xcb-util
        xcb-xkb
        xkbcommon
        xkbcommon-x11
        xcb-xinput
)

endif()

include(CMakeListsGenerated.txt)

# set(target_dir_watch nyla_platform_dir_watch)
# add_library(${target_dir_watch} INTERFACE)
# add_library(nyla::platform::dir_watch ALIAS ${target_dir_watch})
#
# target_sources(${target_dir_watch}
#     INTERFACE
#         platform_dir_watch.h
# )
#
# set(target_audio nyla_platform_audio)
# add_library(${target_audio} INTERFACE)
# add_library(nyla::platform::audio ALIAS ${target_audio})
#
# target_sources(${target_audio}
#     INTERFACE
#         platform_audio.h
# )
#
# elseif (UNIX AND NOT APPLE)
#     # add_subdirectory(linux)
#     # target_link_libraries(${target_platform}
#     #     INTERFACE
#     #         nyla::platform::linux
#     # )
#     # target_link_libraries(${target_dir_watch}
#     #     INTERFACE
#     #         nyla::platform::linux::dir_watch
#     # )
#     # target_link_libraries(${target_audio}
#     #     INTERFACE
#     #         nyla::platform::linux::audio
#     # )
# endif()