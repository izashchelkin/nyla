set(TARGET xcav)

enable_language(C) # needed for tree-sitter grammar parser.c files

set(VENDOR ${CMAKE_SOURCE_DIR}/vendor)

# ─── tree-sitter core ───────────────────────────────────────────────────────
add_library(ts STATIC ${VENDOR}/tree-sitter/lib/src/lib.c)
target_include_directories(ts PUBLIC
    ${VENDOR}/tree-sitter/lib/include
    ${VENDOR}/tree-sitter/lib/src
)

# ─── tree-sitter-c grammar ──────────────────────────────────────────────────
add_library(ts_c STATIC ${VENDOR}/tree-sitter-c/src/parser.c)
target_include_directories(ts_c PUBLIC ${VENDOR}/tree-sitter-c/src)
target_link_libraries(ts_c PUBLIC ts)
set_target_properties(ts_c PROPERTIES LINKER_LANGUAGE C)

# ─── tree-sitter-cpp grammar ────────────────────────────────────────────────
add_library(ts_cpp STATIC
    ${VENDOR}/tree-sitter-cpp/src/parser.c
    ${VENDOR}/tree-sitter-cpp/src/scanner.c
)
target_include_directories(ts_cpp PUBLIC ${VENDOR}/tree-sitter-cpp/src)
target_link_libraries(ts_cpp PUBLIC ts)
set_target_properties(ts_cpp PROPERTIES LINKER_LANGUAGE C)

# ─── tree-sitter-java grammar ───────────────────────────────────────────────
add_library(ts_java STATIC ${VENDOR}/tree-sitter-java/src/parser.c)
target_include_directories(ts_java PUBLIC ${VENDOR}/tree-sitter-java/src)
target_link_libraries(ts_java PUBLIC ts)
set_target_properties(ts_java PROPERTIES LINKER_LANGUAGE C)

# ─── tree-sitter-javascript grammar ─────────────────────────────────────────
add_library(ts_javascript STATIC
    ${VENDOR}/tree-sitter-javascript/src/parser.c
    ${VENDOR}/tree-sitter-javascript/src/scanner.c
)
target_include_directories(ts_javascript PUBLIC ${VENDOR}/tree-sitter-javascript/src)
target_link_libraries(ts_javascript PUBLIC ts)
set_target_properties(ts_javascript PROPERTIES LINKER_LANGUAGE C)

# ─── tree-sitter-typescript grammar ─────────────────────────────────────────
add_library(ts_typescript STATIC
    ${VENDOR}/tree-sitter-typescript/typescript/src/parser.c
    ${VENDOR}/tree-sitter-typescript/typescript/src/scanner.c
)
target_include_directories(ts_typescript PUBLIC ${VENDOR}/tree-sitter-typescript/typescript/src)
target_link_libraries(ts_typescript PUBLIC ts)
set_target_properties(ts_typescript PROPERTIES LINKER_LANGUAGE C)

# ─── tree-sitter-tsx grammar ────────────────────────────────────────────────
add_library(ts_tsx STATIC
    ${VENDOR}/tree-sitter-typescript/tsx/src/parser.c
    ${VENDOR}/tree-sitter-typescript/tsx/src/scanner.c
)
target_include_directories(ts_tsx PUBLIC ${VENDOR}/tree-sitter-typescript/tsx/src)
target_link_libraries(ts_tsx PUBLIC ts)
set_target_properties(ts_tsx PROPERTIES LINKER_LANGUAGE C)

# ─── xcav core library ──────────────────────────────────────────────────
file(GLOB xcav_lib_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)
list(REMOVE_ITEM xcav_lib_sources "${CMAKE_CURRENT_SOURCE_DIR}/main.cc")

add_library(xcav_lib STATIC ${xcav_lib_sources})
target_link_libraries(xcav_lib PUBLIC
    nyla::commons_headless
    ts
    ts_c
    ts_cpp
    ts_java
    ts_javascript
    ts_typescript
    ts_tsx
)
target_include_directories(xcav_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ─── xcav executable ────────────────────────────────────────────────────────
add_executable(${TARGET})
target_link_libraries(${TARGET} PRIVATE
    -static
    nyla::commons_headless
    ts
    ts_c
    ts_cpp
    ts_java
    ts_javascript
    ts_typescript
    ts_tsx
)

# ─── Install ────────────────────────────────────────────────────────────────
install(TARGETS ${TARGET} RUNTIME DESTINATION bin)

include(CMakeListsGenerated.txt)
