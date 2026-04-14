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

add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)

if(MSVC)
    foreach(flag_var 
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE)
        
        string(REPLACE "/RTC1" "" ${flag_var} "${${flag_var}}")
        string(REPLACE "/EHsc" "" ${flag_var} "${${flag_var}}")
    endforeach()

    add_compile_options(
        "/GS-"           
        "/GR-"          
        "/EHs-c-"         
        "/Oi-"             
        "/Zc:threadSafeInit-"
    )

    add_compile_options(
        "/arch:AVX2"
    )
    
    add_link_options(
        "/NODEFAULTLIB"     
        "/SUBSYSTEM:WINDOWS" 
        "/STACK:0x4000,0x4000"
    )

    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NOENTRY")

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        "-ffreestanding"    
        "-fno-builtin"       
        "-fno-stack-protector"
        "-fno-exceptions"
        "-fno-rtti"
        "-fno-strict-aliasing"
        "-fvisibility=hidden"
    )

    add_compile_options(
        "-mavx2"
    )
    
    add_link_options(
        "-nostdlib"            
        "-nodefaultlibs"      
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(nyla)

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