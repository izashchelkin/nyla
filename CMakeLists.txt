cmake_minimum_required(VERSION 4.2)
project(nyla LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(absl CONFIG REQUIRED)

add_subdirectory(nyla)
add_subdirectory(third_party)

file(GLOB_RECURSE NYLA_ALL_CXX_FILES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/nyla/*.cc"
    "${PROJECT_SOURCE_DIR}/nyla/*.cpp"
    "${PROJECT_SOURCE_DIR}/nyla/*.cxx"
    "${PROJECT_SOURCE_DIR}/nyla/*.h"
    "${PROJECT_SOURCE_DIR}/nyla/*.hpp"
    "${PROJECT_SOURCE_DIR}/nyla/*.hh"
    "${PROJECT_SOURCE_DIR}/nyla/*.hxx"

    "${PROJECT_SOURCE_DIR}/nyla/*.hlsl"
)

add_custom_target(format
    COMMAND clang-format -i ${NYLA_ALL_CXX_FILES}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)