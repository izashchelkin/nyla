cmake_minimum_required(VERSION 4.0)
project(nyla LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${PROJECT_SOURCE_DIR})

if(MSVC)
    add_compile_options(
        "/Zc:preprocessor"
        "/MP"
        "/arch:AVX2"
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        "-fno-strict-aliasing"
        "-fvisibility=hidden"
        "-mavx2"
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

file(GLOB_RECURSE NYLA_ALL_CXX_FILES
    CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/nyla/*.cc"
        "${PROJECT_SOURCE_DIR}/nyla/*.h"
        "${PROJECT_SOURCE_DIR}/nyla/*.hlsl"

        "${PROJECT_SOURCE_DIR}/3d_ball_maze/*.cc"
        "${PROJECT_SOURCE_DIR}/3d_ball_maze/*.h"
        "${PROJECT_SOURCE_DIR}/3d_ball_maze/*.hlsl"

        "${PROJECT_SOURCE_DIR}/breakout/*.cc"
        "${PROJECT_SOURCE_DIR}/breakout/*.h"
        "${PROJECT_SOURCE_DIR}/breakout/*.hlsl"

        "${PROJECT_SOURCE_DIR}/shipgame/*.cc"
        "${PROJECT_SOURCE_DIR}/shipgame/*.h"
        "${PROJECT_SOURCE_DIR}/shipgame/*.hlsl"

        "${PROJECT_SOURCE_DIR}/terminal/*.cc"
        "${PROJECT_SOURCE_DIR}/terminal/*.h"
        "${PROJECT_SOURCE_DIR}/terminal/*.hlsl"

        "${PROJECT_SOURCE_DIR}/wm/*.cc"
        "${PROJECT_SOURCE_DIR}/wm/*.h"
        "${PROJECT_SOURCE_DIR}/wm/*.hlsl"


        "${PROJECT_SOURCE_DIR}/screen_inhibitor/*.cc"
        "${PROJECT_SOURCE_DIR}/screen_inhibitor/*.h"
        "${PROJECT_SOURCE_DIR}/screen_inhibitor/*.hlsl"

        "${PROJECT_SOURCE_DIR}/asset_packer/*.cc"
        "${PROJECT_SOURCE_DIR}/asset_packer/*.h"
        "${PROJECT_SOURCE_DIR}/asset_packer/*.hlsl"

        "${PROJECT_SOURCE_DIR}/asset_tool/*.cc"
        "${PROJECT_SOURCE_DIR}/asset_tool/*.h"
        "${PROJECT_SOURCE_DIR}/asset_tool/*.hlsl"

        "${PROJECT_SOURCE_DIR}/psf2_glsl/*.cc"
        "${PROJECT_SOURCE_DIR}/psf2_glsl/*.h"
        "${PROJECT_SOURCE_DIR}/psf2_glsl/*.hlsl"

        "${PROJECT_SOURCE_DIR}/git_ui/*.cc"
        "${PROJECT_SOURCE_DIR}/git_ui/*.h"
        "${PROJECT_SOURCE_DIR}/git_ui/*.hlsl"
)

# xcav: non-recursive glob to avoid picking up tests/fixtures + tests/expected
file(GLOB NYLA_XCAV_CXX_FILES
    CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/xcav/*.cc"
        "${PROJECT_SOURCE_DIR}/xcav/*.h"
        "${PROJECT_SOURCE_DIR}/xcav/*.inl"
)
list(APPEND NYLA_ALL_CXX_FILES ${NYLA_XCAV_CXX_FILES})

file(GLOB NYLA_CODING_AGENT_CXX_FILES
    CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/coding_agent/*.cc"
        "${PROJECT_SOURCE_DIR}/coding_agent/*.h"
)
list(APPEND NYLA_ALL_CXX_FILES ${NYLA_CODING_AGENT_CXX_FILES})

add_custom_target(format
    COMMAND clang-format -i ${NYLA_ALL_CXX_FILES}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)



add_subdirectory(nyla)

add_subdirectory(asset_packer)
add_subdirectory(asset_tool)
add_subdirectory(3d_ball_maze)
add_subdirectory(breakout)
add_subdirectory(shipgame)
add_subdirectory(terminal)
add_subdirectory(wm)
add_subdirectory(screen_inhibitor)
add_subdirectory(psf2_glsl)
add_subdirectory(git_ui)
add_subdirectory(xcav)
add_subdirectory(coding_agent)
add_subdirectory(tests/wm)
add_subdirectory(tests/json_parser)
