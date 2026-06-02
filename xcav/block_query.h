#pragma once

#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"
#include "xcav/language.h"
#include "xcav/tree_sitter_util.h"

struct TSTree;
struct TSNode;
struct region_alloc;

namespace nyla
{

// ─── Block info ─────────────────────────────────────────────────────────────

struct block_info
{
    inline_string<128> type; // e.g. "function_definition", "if_statement"
    inline_string<128> name; // block name (e.g. "DetectLanguage"), empty if unnamed
    uint32_t startLine;      // 0-indexed
    uint32_t endLine;        // 0-indexed
    uint32_t startByte;
    uint32_t endByte;
};

// Result of ReadBlock: the block's text plus structural context.
struct read_block_info
{
    inline_string<256> path; // e.g. "Point::GetX" or "foo"
    inline_string<128> type; // e.g. "function_definition"
    uint32_t startLine;      // 0-indexed
    uint32_t endLine;        // 0-indexed
    uint32_t startByte;      // byte offset in source file
    uint32_t endByte;        // byte offset in source file
    // Un-indented block text, region-allocated.
    byteview text;
};

// Internal: located block with parsed tree and source.
struct block_loc
{
    byteview source;
    TSTree *tree;
    TSNode block;
    node_range range;
    const char *type; // ts_node_type(block), valid while tree is alive
};

// ─── Output helpers ─────────────────────────────────────────────────────────

// Map a tree-sitter node type to a short human-readable label.
// e.g. "function_definition" → "func", "struct_specifier" → "struct".
auto BlockTypeLabel(const char *type) -> const char *;

// ─── Block classification ──────────────────────────────────────────────────

auto IsContainerType(const char *type) -> bool;
auto IsStructuralType(const char *t) -> bool;
auto IsInnerBlockType(const char *t) -> bool;

// ─── Queries ────────────────────────────────────────────────────────────────

// Check if filePath is a regular file (exists and is not a directory).
auto IsRegularFile(byteview filePath, region_alloc &alloc) -> bool;

// Locate the structural block containing the given line (0-indexed).
// Returns a block_loc with tree=nullptr on failure.
auto LocateBlock(byteview filePath, uint32_t line, region_alloc &alloc) -> block_loc;

// Extract the name of a tree-sitter node from source text.
auto NodeName(TSNode node, byteview source) -> inline_string<128>;

// Collect all top-level named blocks from the parsed tree into result.
void CollectBlockNodes(TSNode node, inline_vec<block_info, 256> &result, int depth, bool recurse,
                       byteview source, source_language lang = source_language::Unknown);

// Parse a file and retrieve all top-level named blocks.
auto ListBlocks(byteview filePath, region_alloc &alloc) -> inline_vec<block_info, 256>;

// Read the structural block containing `line` (0-indexed) and return it
// with its text un-indented and a human-readable structural path.
auto ReadBlock(byteview filePath, uint32_t line, region_alloc &alloc, bool raw = false) -> read_block_info;

} // namespace nyla
