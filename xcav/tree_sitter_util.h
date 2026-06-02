#pragma once

// Thin C++ wrapper around tree-sitter C API.
// Owns parser and tree lifetimes; exposes node queries as nyla-friendly types.

#include <tree_sitter/api.h>

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"

namespace nyla
{

// ─── Parser ────────────────────────────────────────────────────────────────

// Parse UTF-8 source code with the given language.
// Returns a tree that must be freed with TreeDelete.
auto ParseSource(byteview source, const TSLanguage *lang) -> TSTree *;
void TreeDelete(TSTree *tree);

// ─── Node info ──────────────────────────────────────────────────────────────

// Byte range of a node within the source text.
struct node_range
{
    uint32_t startByte;
    uint32_t endByte;
    uint32_t startRow; // 0-indexed
    uint32_t endRow;   // 0-indexed (line of the last character)
};

auto NodeType(const TSNode &node) -> const char *;
auto NodeRange(const TSNode &node) -> node_range;
auto NodeIsNamed(const TSNode &node) -> bool;
auto NodeHasError(const TSNode &node) -> bool;
auto NodeChildCount(const TSNode &node) -> uint32_t;
auto NodeChild(const TSNode &node, uint32_t index) -> TSNode;
auto NodeNamedChild(const TSNode &node, uint32_t index) -> TSNode;
auto NodeParent(const TSNode &node) -> TSNode;
auto NodeNextSibling(const TSNode &node) -> TSNode;

// ─── Descendant queries ─────────────────────────────────────────────────────

// Find the smallest named node that contains the given line (0-indexed).
// Returns a null node (ts_node_is_null) if not found.
auto NodeAtLine(const TSNode &root, uint32_t line, byteview source) -> TSNode;

// Collect all named descendant nodes matching `nodeType` (e.g. "function_definition").
// Writes up to `maxCount` nodes into `out`; returns the count written.
auto CollectNodesOfType(const TSNode &root, const char *nodeType, TSNode *out, uint32_t maxCount) -> uint32_t;

} // namespace nyla
