// Test fixture -- raw string literal vends " (tree-sitter-cpp parse error)
const char* json = R"({
    "key": "value"
})";

int get_value() {
    return 99;
}
