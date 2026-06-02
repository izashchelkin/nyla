#include "xcav/language.h"
#include <tree_sitter/api.h>

extern "C"
{
    TSLanguage *tree_sitter_c();
    TSLanguage *tree_sitter_cpp();
    TSLanguage *tree_sitter_java();
    TSLanguage *tree_sitter_javascript();
    TSLanguage *tree_sitter_typescript();
    TSLanguage *tree_sitter_tsx();
}

namespace nyla
{

// ─── DetectLanguage ─────────────────────────────────────────────────────────

auto DetectLanguage(byteview filePath) -> source_language
{
    // Find the last '.' in the path
    uint64_t dotPos = filePath.size;
    for (uint64_t i = filePath.size; i > 0; --i)
    {
        if (filePath.data[i - 1] == '.')
        {
            dotPos = i - 1;
            break;
        }
        if (filePath.data[i - 1] == '/')
            break;
    }

    if (dotPos >= filePath.size)
        return source_language::Unknown;

    byteview ext{filePath.data + dotPos + 1, filePath.size - dotPos - 1};

    // Match extension
    if (ext.size == 1 && ext.data[0] == 'c')
        return source_language::C;
    // .h files: assume C++ (most .h files in a C++ codebase are C++ headers;
    // tree-sitter-cpp handles plain C structurally for block detection purposes)
    if (ext.size == 1 && ext.data[0] == 'h')
        return source_language::Cpp;
    if (ext.size == 2 && ext.data[0] == 'c' && (ext.data[1] == 'c' || ext.data[1] == 'p' || ext.data[1] == 'x'))
        return source_language::Cpp;
    if (ext.size == 2 && ext.data[0] == 'h' && ext.data[1] == 'h')
        return source_language::Cpp;
    if (ext.size == 2 && ext.data[0] == 'C' && (ext.data[1] == 'C' || ext.data[1] == 'P'))
        return source_language::Cpp;
    if (ext.size == 3 && ext.data[0] == 'c' && ext.data[1] == 'p' && ext.data[2] == 'p')
        return source_language::Cpp;
    if (ext.size == 3 && ext.data[0] == 'c' && ext.data[1] == 'x' && ext.data[2] == 'x')
        return source_language::Cpp;
    if (ext.size == 3 && ext.data[0] == 'h' && ext.data[1] == 'p' && ext.data[2] == 'p')
        return source_language::Cpp;
    if (ext.size == 3 && ext.data[0] == 'h' && ext.data[1] == 'x' && ext.data[2] == 'x')
        return source_language::Cpp;
    if ((ext.size == 4 && ext.data[0] == 'j' && ext.data[1] == 'a' && ext.data[2] == 'v' && ext.data[3] == 'a'))
        return source_language::Java;

    // JavaScript
    if (ext.size == 2 && ext.data[0] == 'j' && ext.data[1] == 's')
        return source_language::JavaScript;
    if (ext.size == 3 && ext.data[0] == 'j' && ext.data[1] == 's' && ext.data[2] == 'x')
        return source_language::Tsx;
    if (ext.size == 3 && ext.data[0] == 'm' && ext.data[1] == 'j' && ext.data[2] == 's')
        return source_language::JavaScript;
    if (ext.size == 3 && ext.data[0] == 'c' && ext.data[1] == 'j' && ext.data[2] == 's')
        return source_language::JavaScript;

    // TypeScript (check before 2-char .ts to avoid ambiguity with other .ts uses)
    if (ext.size == 3 && ext.data[0] == 't' && ext.data[1] == 's' && ext.data[2] == 'x')
        return source_language::Tsx;
    if (ext.size == 3 && ext.data[0] == 'm' && ext.data[1] == 't' && ext.data[2] == 's')
        return source_language::TypeScript;
    if (ext.size == 3 && ext.data[0] == 'c' && ext.data[1] == 't' && ext.data[2] == 's')
        return source_language::TypeScript;
    if (ext.size == 2 && ext.data[0] == 't' && ext.data[1] == 's')
        return source_language::TypeScript;

    return source_language::Unknown;
}

// ─── GrammarForLanguage ─────────────────────────────────────────────────────

auto GrammarForLanguage(source_language lang) -> const TSLanguage *
{
    switch (lang)
    {
    case source_language::C:
        return tree_sitter_c();
    case source_language::Cpp:
        return tree_sitter_cpp();
    case source_language::Java:
        return tree_sitter_java();
    case source_language::JavaScript:
        return tree_sitter_javascript();
    case source_language::TypeScript:
        return tree_sitter_typescript();
    case source_language::Tsx:
        return tree_sitter_tsx();
    default:
        return nullptr;
    }
}

} // namespace nyla
