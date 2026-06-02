#pragma once

#include "nyla/commons/span.h"

struct TSLanguage;

namespace nyla
{

// ─── Language ───────────────────────────────────────────────────────────────

enum class source_language : uint8_t
{
    Unknown,
    C,
    Cpp,
    Java,
    JavaScript,
    TypeScript,
    Tsx,
};

// Detect language from file extension.
auto DetectLanguage(byteview filePath) -> source_language;

// Get the tree-sitter grammar for a language. Returns nullptr if unsupported.
auto GrammarForLanguage(source_language lang) -> const TSLanguage *;

} // namespace nyla
