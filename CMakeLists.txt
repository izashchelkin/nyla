cmake_minimum_required(VERSION 4.0)
project(nyla LANGUAGES CXX)
enable_testing()


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

        "${PROJECT_SOURCE_DIR}/wm/*.cc"
        "${PROJECT_SOURCE_DIR}/wm/*.h"
        "${PROJECT_SOURCE_DIR}/wm/*.hlsl"
)

add_custom_target(format
    COMMAND clang-format -i ${NYLA_ALL_CXX_FILES}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)



add_subdirectory(nyla)

add_subdirectory(3d_ball_maze)
add_subdirectory(breakout)
add_subdirectory(wm)
