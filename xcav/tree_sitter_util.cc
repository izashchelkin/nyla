#include "xcav/tree_sitter_util.h"

#include <tree_sitter/api.h>

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"

namespace nyla
{

// ─── Parser ────────────────────────────────────────────────────────────────

auto ParseSource(byteview source, const TSLanguage *lang) -> TSTree *
{
    TSParser *parser = ts_parser_new();
    bool ok = ts_parser_set_language(parser, lang);
    ASSERT(ok);

    TSTree *tree = ts_parser_parse_string(parser, nullptr, (const char *)source.data, (uint32_t)source.size);

    ts_parser_delete(parser);
    return tree;
}

void TreeDelete(TSTree *tree)
{
    ts_tree_delete(tree);
}

// ─── Node info ──────────────────────────────────────────────────────────────

auto NodeRange(const TSNode &node) -> node_range
{
    return node_range{
        .startByte = ts_node_start_byte(node),
        .endByte = ts_node_end_byte(node),
        .startRow = ts_node_start_point(node).row,
        .endRow = ts_node_end_point(node).row,
    };
}



// ─── Descendant queries ─────────────────────────────────────────────────────

auto NodeAtLine(const TSNode &root, uint32_t targetLine, byteview source) -> TSNode
{
    // Walk down the tree to find the deepest named node containing targetLine.
    TSNode best{}; // zero-init = null node
    TSNode current = root;

    // First, find any node that contains a byte in the target line.
    // targetLine is 0-indexed. Find the byte offset of the first character
    // on that line.
    uint32_t byteOffset = 0;
    uint32_t line = 0;
    for (uint32_t i = 0; i < source.size; ++i)
    {
        if (line == targetLine)
        {
            byteOffset = i;
            break;
        }
        if (source.data[i] == '\n')
            ++line;
    }

    // Skip past leading whitespace on this line so byteOffset lands
    // inside the first named node on the line. Without this, a line that
    // starts with whitespace (e.g. "    int foo") would have byteOffset
    // pointing at a space, which is outside any child node.
    while (byteOffset < source.size && source.data[byteOffset] != '\n' &&
           (source.data[byteOffset] == ' ' || source.data[byteOffset] == '\t'))
        ++byteOffset;

    // Walk down to the deepest named node at this byte offset.
    for (;;)
    {
        TSNode child = ts_node_first_child_for_byte(current, byteOffset);
        if (ts_node_is_null(child))
            break;

        // If child doesn't actually contain the byte, stop.
        if (ts_node_start_byte(child) > byteOffset || ts_node_end_byte(child) <= byteOffset)
            break;

        current = child;

        // Track the deepest named node we've seen.
        if (ts_node_is_named(current) && !ts_node_is_error(current))
            best = current;
    }

    return best;
}



} // namespace nyla
