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
        xcb-randr
        util
        PkgConfig::ALSA
)

endif()

include(CMakeListsGenerated.txt)

# ─── Headless static library (no X11/Vulkan/ALSA) ───
# CLI tools link against this to avoid pulling in graphical dependencies.
# Compiled with -DNYLA_HEADLESS; platform_linux.cc skips all X11 code paths.

set(TARGET_HEADLESS nyla_commons_headless)
add_library(${TARGET_HEADLESS} STATIC)
add_library(nyla::commons_headless ALIAS ${TARGET_HEADLESS})
target_compile_definitions(${TARGET_HEADLESS} PRIVATE NYLA_HEADLESS)
target_link_libraries(${TARGET_HEADLESS} PUBLIC Threads::Threads)
target_sources(${TARGET_HEADLESS} PRIVATE
    libmain.cc
    mempage_pool.cc
    region_alloc.cc
    fmt.cc
    platform_linux.cc
    time_linux.cc
)