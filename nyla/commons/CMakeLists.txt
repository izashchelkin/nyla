set(TARGET nyla_commons)
add_library(${TARGET})
add_library(nyla::commons ALIAS ${TARGET})

if(BUILD_SHARED_LIBS)
    target_compile_definitions(${TARGET}
        PRIVATE DLL_EXPORT
        INTERFACE DLL_IMPORT
    )
endif()

find_package(Vulkan)
find_package(Threads REQUIRED)

# if (WIN32)
#     set(D3D12_FOUND TRUE)
# endif()

target_link_libraries(${TARGET}
    PUBLIC
        Threads::Threads
)

if (Vulkan_FOUND)
    target_link_libraries(${TARGET}
        PUBLIC
            Vulkan::Vulkan
    )
endif()

if (D3D12_FOUND)
    target_link_libraries(${TARGET}
        PRIVATE
            d3d12
            dxgi
            dxguid
    )
endif()

if (WIN32)
    target_link_libraries(${TARGET}
        PRIVATE
            XInput
            BCrypt
            ole32
    )
else()

find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

target_link_libraries(${TARGET}
    PUBLIC
        xcb
        xcb-errors
        xcb-util
        xcb-xkb
        xkbcommon
        xkbcommon-x11
        xcb-xinput
        PkgConfig::ALSA
)

endif()

include(CMakeListsGenerated.txt)