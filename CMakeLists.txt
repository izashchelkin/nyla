cmake_minimum_required(VERSION 4.0)
project(nyla LANGUAGES CXX)
enable_testing()

if(MSVC)
 add_compile_options( /MP )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${PROJECT_SOURCE_DIR})

if(MSVC)
    add_link_options(
        "/NODEFAULTLIB"
    )
    
    add_compile_options(
        "/GS-"      # Disable buffer security check 
        "/GR-"      # Disable RTTI 
        "/EHs-c-"   # Disable C++ exceptions 
    )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_link_options(
        "-nostdlib" 
        "-nodefaultlibs"
    )
    
    add_compile_options(
        "-fno-builtin"         # Stop compiler from replacing loops
        "-fno-exceptions"      # Disable C++ exceptions
        "-fno-rtti"            # Disable RTTI
        "-fno-stack-protector" # Disable stack guards
    )
endif()

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