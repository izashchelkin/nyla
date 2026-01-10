find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3)

add_library(nyla_debugfs
    debugfs.cc
    debugfs.h
)
add_library(nyla::debugfs ALIAS nyla_debugfs)

target_include_directories(nyla_debugfs
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${FUSE_INCLUDE_DIRS}
)

target_link_libraries(nyla_debugfs
    PUBLIC
        nyla::commons
        ${FUSE_LIBRARIES}
)