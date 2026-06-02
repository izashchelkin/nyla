#pragma once

#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"

namespace nyla
{

// ─── Text utilities ─────────────────────────────────────────────────────────

// Byte offset of the start of the given 0-indexed line within source.
auto LineStartOffset(byteview source, uint32_t line) -> uint32_t;

// Byte offset of the end of the given 0-indexed line (position before '\n').
auto LineEndOffset(byteview source, uint32_t line) -> uint32_t;

// Count of leading spaces/tabs at the given byte offset within source.
auto LineIndent(byteview source, uint32_t lineStart) -> uint32_t;

// C-string equality check.
auto StrEq(const char *a, const char *b) -> bool;

// Extract the include path ("..." or <...>) from a #include / import block.
auto IncludePath(byteview blockText) -> inline_string<128>;

// Normalize Unicode punctuation in-place:
// em-dash → "--", en-dash → "-", arrows → "->"/"<-", smart quotes → ASCII.
void NormalizeText(inline_vec<uint8_t, 65536> &buf);

} // namespace nyla
