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

auto NodeType(const TSNode &node) -> const char *
{
    return ts_node_type(node);
}

auto NodeRange(const TSNode &node) -> node_range
{
    return node_range{
        .startByte = ts_node_start_byte(node),
        .endByte = ts_node_end_byte(node),
        .startRow = ts_node_start_point(node).row,
        .endRow = ts_node_end_point(node).row,
    };
}

auto NodeIsNamed(const TSNode &node) -> bool
{
    return ts_node_is_named(node);
}

auto NodeHasError(const TSNode &node) -> bool
{
    return ts_node_has_error(node);
}

auto NodeChildCount(const TSNode &node) -> uint32_t
{
    return ts_node_child_count(node);
}

auto NodeChild(const TSNode &node, uint32_t index) -> TSNode
{
    return ts_node_child(node, index);
}

auto NodeNamedChild(const TSNode &node, uint32_t index) -> TSNode
{
    return ts_node_named_child(node, index);
}

auto NodeParent(const TSNode &node) -> TSNode
{
    return ts_node_parent(node);
}

auto NodeNextSibling(const TSNode &node) -> TSNode
{
    return ts_node_next_sibling(node);
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

auto CollectNodesOfType(const TSNode &root, const char *nodeType, TSNode *out, uint32_t maxCount) -> uint32_t
{
    uint32_t count = 0;

    struct stack_entry
    {
        TSNode node;
        uint32_t nextChild;
    };

    inline_vec<stack_entry, 256> stack{};
    InlineVec::Append(stack, stack_entry{root, 0});

    while (stack.size > 0 && count < maxCount)
    {
        stack_entry &top = InlineVec::Back(stack);

        if (top.nextChild == 0)
        {
            if (ts_node_is_named(top.node) && !ts_node_is_error(top.node))
            {
                const char *type = ts_node_type(top.node);
                const char *a = type;
                const char *b = nodeType;
                bool match = true;
                while (*a && *b)
                {
                    if (*a != *b)
                    {
                        match = false;
                        break;
                    }
                    ++a;
                    ++b;
                }
                if (match && *a == 0 && *b == 0)
                    out[count++] = top.node;
            }
        }

        if (top.nextChild < ts_node_child_count(top.node))
        {
            TSNode child = ts_node_child(top.node, top.nextChild);
            ++top.nextChild;
            InlineVec::Append(stack, stack_entry{child, 0});
        }
        else
        {
            InlineVec::PopBack(stack);
        }
    }

    return count;
}

} // namespace nyla
