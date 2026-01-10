file(GLOB SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(TARGET breakout)

if (WIN32)
    add_executable(${TARGET} WIN32)
else()
    add_executable(${TARGET})
endif()

target_sources(${TARGET} PRIVATE ${SOURCES} ${HEADERS})

target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR})

target_link_libraries(${TARGET} PRIVATE nyla::engine)

if (WIN32)
    target_link_libraries(${TARGET} PRIVATE user32)
    # target_link_libraries(${TARGET} PRIVATE gdi32 shell32 ole32)
endif()