#include "xcav/block_query.h"
#include "xcav/language.h"
#include "xcav/text_util.h"

#include <sys/stat.h>

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

// ─── Block classification ──────────────────────────────────────────────────

auto IsContainerType(const char *type) -> bool
{
    // Only descend into namespaces and program-level containers.
    // Treat classes, structs, and enums as opaque blocks.
    return StrEq(type, "namespace_definition") || StrEq(type, "declaration_list") || StrEq(type, "program") ||
           StrEq(type, "statement_block");
}

auto IsStructuralType(const char *t) -> bool
{
    return StrEq(t, "function_definition") || StrEq(t, "function_declaration") || StrEq(t, "method_declaration") ||
           StrEq(t, "method_definition") || StrEq(t, "struct_specifier") || StrEq(t, "class_specifier") ||
           StrEq(t, "class_declaration") || StrEq(t, "constructor_declaration") || StrEq(t, "enum_specifier") ||
           StrEq(t, "enum_declaration") || StrEq(t, "union_specifier") || StrEq(t, "declaration") ||
           StrEq(t, "template_declaration") || StrEq(t, "comment") || StrEq(t, "preproc_include") ||
           StrEq(t, "namespace_definition") ||
           // TypeScript/JavaScript
           StrEq(t, "generator_function_declaration") || StrEq(t, "arrow_function") || StrEq(t, "class") ||
           StrEq(t, "interface_declaration") || StrEq(t, "type_alias_declaration") || StrEq(t, "export_statement") ||
           StrEq(t, "import_statement") || StrEq(t, "lexical_declaration") || StrEq(t, "variable_declaration") ||
           StrEq(t, "abstract_class_declaration") || StrEq(t, "ambient_declaration");
}

// ─── BlockTypeLabel ─────────────────────────────────────────────────────────

auto BlockTypeLabel(const char *t) -> const char *
{
    if (StrEq(t, "function_definition") || StrEq(t, "function_declaration") || StrEq(t, "method_definition") ||
        StrEq(t, "method_declaration") || StrEq(t, "constructor_declaration") ||
        StrEq(t, "generator_function_declaration") || StrEq(t, "arrow_function"))
        return "func";
    if (StrEq(t, "struct_specifier"))
        return "struct";
    if (StrEq(t, "class_specifier") || StrEq(t, "class_declaration") || StrEq(t, "abstract_class_declaration") ||
        StrEq(t, "class"))
        return "class";
    if (StrEq(t, "enum_specifier") || StrEq(t, "enum_declaration"))
        return "enum";
    if (StrEq(t, "union_specifier"))
        return "union";
    if (StrEq(t, "declaration") || StrEq(t, "ambient_declaration"))
        return "decl";
    if (StrEq(t, "template_declaration"))
        return "template";
    if (StrEq(t, "preproc_include"))
        return "include";
    if (StrEq(t, "namespace_definition"))
        return "namespace";
    if (StrEq(t, "interface_declaration"))
        return "interface";
    if (StrEq(t, "type_alias_declaration"))
        return "type";
    if (StrEq(t, "export_statement"))
        return "export";
    if (StrEq(t, "import_statement"))
        return "import";
    if (StrEq(t, "lexical_declaration") || StrEq(t, "variable_declaration"))
        return "var";
    return t;
}

// ─── IsRegularFile ──────────────────────────────────────────────────────────

auto IsRegularFile(byteview filePath, region_alloc &alloc) -> bool
{
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(pathBuf.data, filePath.data, filePath.size);
    pathBuf.data[filePath.size] = 0;
    struct stat st;
    if (stat((const char *)pathBuf.data, &st) != 0)
        return true; // doesn't exist — let FileOpen handle it
    return S_ISREG(st.st_mode);
}

// ─── LocateBlock ────────────────────────────────────────────────────────────

auto LocateBlock(byteview filePath, uint32_t line, region_alloc &alloc) -> block_loc
{
    block_loc result{};

    if (!IsRegularFile(filePath, alloc))
    {
        LOG("ERROR: not a regular file '%.*s'", (int)filePath.size, filePath.data);
        return result;
    }

    file_handle fh = FileOpen(filePath, FileOpenMode::Read);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot open '%.*s'", (int)filePath.size, filePath.data);
        return result;
    }
    result.source = FileReadFully(alloc, fh);
    FileClose(fh);

    const TSLanguage *grammar = GrammarForLanguage(DetectLanguage(filePath));
    if (!grammar)
    {
        LOG("ERROR: unsupported file type for '%.*s'", (int)filePath.size, filePath.data);
        return result;
    }
    result.tree = ParseSource(result.source, grammar);
    TSNode root = ts_tree_root_node(result.tree);

    TSNode node = NodeAtLine(root, line, result.source);
    if (ts_node_is_null(node))
    {
        LOG("ERROR: no structural block found at line %u — use xcav blocks to browse", line);
        TreeDelete(result.tree);
        result.tree = nullptr;
        return result;
    }

    // Walk up to a structural block
    result.block = node;
    while (!IsStructuralType(ts_node_type(result.block)))
    {
        TSNode parent = ts_node_parent(result.block);
        if (ts_node_is_null(parent))
            break;
        result.block = parent;
    }

    // If we landed on a comment, prefer the next non-comment sibling.
    // Only keep the comment if no other meaningful block follows.
    if (StrEq(ts_node_type(result.block), "comment"))
    {
        TSNode parent = ts_node_parent(result.block);
        if (!ts_node_is_null(parent))
        {
            uint32_t count = ts_node_child_count(parent);
            bool foundSelf = false;
            for (uint32_t i = 0; i < count; ++i)
            {
                TSNode child = ts_node_child(parent, i);
                if (!foundSelf)
                {
                    if (ts_node_eq(child, result.block))
                        foundSelf = true;
                }
                else
                {
                    const char *ct = ts_node_type(child);
                    if (IsStructuralType(ct) && !StrEq(ct, "comment"))
                    {
                        result.block = child;
                        break;
                    }
                }
            }
        }
    }

    result.range = NodeRange(result.block);
    result.type = ts_node_type(result.block);
    return result;
}

// ─── NodeName ───────────────────────────────────────────────────────────────

auto NodeName(TSNode node, byteview source) -> inline_string<128>
{
    inline_string<128> name{};
    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i)
    {
        TSNode child = ts_node_child(node, i);
        const char *typeStr = ts_node_type(child);
        // For container nodes (declarations, specifiers, export statements):
        // the name is nested inside, recurse to find it.
        if (StrEq(typeStr, "template_declaration") || StrEq(typeStr, "class_specifier") ||
            StrEq(typeStr, "struct_specifier") || StrEq(typeStr, "enum_specifier") ||
            StrEq(typeStr, "function_declaration") || StrEq(typeStr, "function_definition") ||
            StrEq(typeStr, "method_definition") || StrEq(typeStr, "variable_declarator") ||
            StrEq(typeStr, "lexical_declaration") || StrEq(typeStr, "variable_declaration") ||
            StrEq(typeStr, "export_statement"))
        {
            auto innerName = NodeName(child, source);
            if (innerName.size > 0)
                return innerName;
        }

        // Look for identifier-like children.
        // For nodes with multiple identifiers (e.g. Java method_declaration
        // where return type "T" and method name "bar" are both identifier
        // nodes), prefer the last identifier child (the method name).
        if (StrEq(typeStr, "identifier") || StrEq(typeStr, "namespace_identifier") ||
            StrEq(typeStr, "function_declarator") || StrEq(typeStr, "field_identifier") ||
            StrEq(typeStr, "property_identifier") || StrEq(typeStr, "statement_identifier") ||
            StrEq(typeStr, "type_identifier"))
        {
            // Recurse into function_declarator to find the inner identifier
            if (StrEq(typeStr, "function_declarator"))
            {
                uint32_t gc = ts_node_child_count(child);
                for (uint32_t j = 0; j < gc; ++j)
                {
                    TSNode gcNode = ts_node_child(child, j);
                    if (StrEq(ts_node_type(gcNode), "identifier") ||
                        StrEq(ts_node_type(gcNode), "qualified_identifier"))
                    {
                        node_range r = NodeRange(gcNode);
                        for (uint32_t k = r.startByte; k < r.endByte && name.size < 127; ++k)
                            InlineVec::Append(name, source.data[k]);
                        return name;
                    }
                }
            }
            // Remember the last identifier/type_identifier found;
            // for Java methods this picks the method name over the return type.
            node_range r = NodeRange(child);
            name.size = 0;
            for (uint32_t k = r.startByte; k < r.endByte && name.size < 127; ++k)
                InlineVec::Append(name, source.data[k]);
            // Continue iterating -- later identifiers override earlier ones.
        }
    }
    return name;
}

// ─── ExtractMethodSignature ─────────────────────────────────────────────────
// Build a method/constructor signature for display in xcav blocks output.
// Extracts modifiers, return type (methods only), name, parameters, and
// throws clause directly from the tree-sitter CST — zero semantic analysis.
// Returns empty string for non-method blocks.

auto ExtractMethodSignature(TSNode node, byteview source) -> inline_string<512>
{
    inline_string<512> sig{};

    const char *nodeType = ts_node_type(node);
    bool isConstructor = StrEq(nodeType, "constructor_declaration");
    bool isMethod = StrEq(nodeType, "method_declaration");

    if (!isConstructor && !isMethod)
        return sig;

    // ── Modifiers (skip annotations) ──
    // modifiers are anonymous keyword tokens ('public', 'static', 'final', etc.)
    // mixed with named annotation/marker_annotation nodes.
    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i)
    {
        TSNode child = ts_node_child(node, i);
        if (!StrEq(ts_node_type(child), "modifiers"))
            continue;

        uint32_t mc = ts_node_child_count(child);
        bool first = true;
        for (uint32_t mi = 0; mi < mc; ++mi)
        {
            TSNode mnode = ts_node_child(child, mi);
            // Skip annotations (named nodes). Modifier keywords are anonymous.
            if (ts_node_is_named(mnode))
                continue;

            if (!first && sig.size > 0)
                InlineVec::Append(sig, (uint8_t)' ');
            first = false;

            node_range r = NodeRange(mnode);
            for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                InlineVec::Append(sig, source.data[k]);
        }
        if (sig.size > 0)
            InlineVec::Append(sig, (uint8_t)' ');
        break; // only one modifiers node
    }

    // ── Return type (methods only, not constructors) ──
    if (!isConstructor)
    {
        TSNode retType = ts_node_child_by_field_name(node, "type", 4);
        if (!ts_node_is_null(retType))
        {
            node_range r = NodeRange(retType);
            for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                InlineVec::Append(sig, source.data[k]);
            InlineVec::Append(sig, (uint8_t)' ');
        }
    }

    // ── Method name ──
    TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(nameNode))
    {
        node_range r = NodeRange(nameNode);
        for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
            InlineVec::Append(sig, source.data[k]);
    }

    // ── Parameters ──
    InlineVec::Append(sig, (uint8_t)'(');
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params))
    {
        uint32_t pc = ts_node_child_count(params);
        bool first = true;
        for (uint32_t i = 0; i < pc; ++i)
        {
            TSNode p = ts_node_child(params, i);
            if (!ts_node_is_named(p))
                continue;
            const char *pt = ts_node_type(p);

            if (!first)
            {
                InlineVec::Append(sig, (uint8_t)',');
                InlineVec::Append(sig, (uint8_t)' ');
            }
            first = false;

            if (StrEq(pt, "spread_parameter"))
            {
                // varargs: use full text (type + '...' + name)
                node_range r = NodeRange(p);
                for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                    InlineVec::Append(sig, source.data[k]);
            }
            else if (StrEq(pt, "formal_parameter"))
            {
                TSNode pType = ts_node_child_by_field_name(p, "type", 4);
                TSNode pName = ts_node_child_by_field_name(p, "name", 4);
                if (!ts_node_is_null(pType))
                {
                    node_range r = NodeRange(pType);
                    for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                        InlineVec::Append(sig, source.data[k]);
                }
                if (!ts_node_is_null(pName))
                {
                    InlineVec::Append(sig, (uint8_t)' ');
                    node_range r = NodeRange(pName);
                    for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                        InlineVec::Append(sig, source.data[k]);
                }
            }
            else
            {
                // receiver_parameter, inferred_parameters, etc.
                node_range r = NodeRange(p);
                for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                    InlineVec::Append(sig, source.data[k]);
            }
        }
    }
    InlineVec::Append(sig, (uint8_t)')');

    // ── Throws clause ──
    // throws is an unnamed child of method_declaration / constructor_declaration.
    for (uint32_t i = 0; i < childCount; ++i)
    {
        TSNode child = ts_node_child(node, i);
        if (StrEq(ts_node_type(child), "throws"))
        {
            InlineVec::Append(sig, (uint8_t)' ');
            node_range r = NodeRange(child);
            for (uint32_t k = r.startByte; k < r.endByte && sig.size < 511; ++k)
                InlineVec::Append(sig, source.data[k]);
            break;
        }
    }

    return sig;
}

// ─── CollectBlockNodes ──────────────────────────────────────────────────────

void CollectBlockNodes(TSNode node, inline_vec<block_info, 256> &result, int depth, bool recurse, byteview source,
                       source_language lang)
{
    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i)
    {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_is_named(child) || ts_node_is_error(child))
            continue;

        const char *typeStr = ts_node_type(child);
        const char *typeCopy = typeStr; // saved before the copy loop advances it

        // Java: recurse into class/interface/enum bodies to find methods.
        // Tree-sitter Java may expose method_declaration as children of both
        // the declaration and the body node, so we deduplicate by position.
        bool isJavaContainer =
            (lang == source_language::Java) && recurse &&
            (StrEq(typeStr, "class_declaration") || StrEq(typeStr, "interface_declaration") ||
             StrEq(typeStr, "enum_declaration") || StrEq(typeStr, "class_body") || StrEq(typeStr, "interface_body") ||
             StrEq(typeStr, "enum_body") || StrEq(typeStr, "enum_body_declarations"));
        if (isJavaContainer)
            CollectBlockNodes(child, result, depth, true, source, lang);

        if (IsContainerType(typeStr) && recurse)
        {
            CollectBlockNodes(child, result, depth, true, source, lang);
        }
        else if (IsStructuralType(typeStr) && !StrEq(typeStr, "comment") && !StrEq(typeStr, "preproc_include") &&
                 !StrEq(typeStr, "import_statement"))
        {
            node_range r = NodeRange(child);

            inline_string<128> typeName{};
            while (*typeCopy && typeName.size < 127)
            {
                InlineVec::Append(typeName, (uint8_t)*typeCopy);
                ++typeCopy;
            }

            inline_string<128> name = NodeName(child, source);

            // Extract method signature for Java method-like blocks.
            inline_string<512> signature{};
            if (lang == source_language::Java)
                signature = ExtractMethodSignature(child, source);

            // Filter nameless single-line declarations (forward decls,
            // trivial variable declarations) -- never useful targets.
            if (StrEq(typeStr, "declaration") && r.startRow == r.endRow && name.size == 0)
                continue;
            // Collect Java annotations from modifiers/annotation children.
            inline_string<64> annotation{};
            if (lang == source_language::Java)
            {
                uint32_t ac = ts_node_child_count(child);
                for (uint32_t ai = 0; ai < ac; ++ai)
                {
                    TSNode anode = ts_node_child(child, ai);
                    if (!ts_node_is_named(anode))
                        continue;
                    const char *aType = ts_node_type(anode);
                    if (StrEq(aType, "modifiers"))
                    {
                        uint32_t mc = ts_node_child_count(anode);
                        for (uint32_t mi = 0; mi < mc; ++mi)
                        {
                            TSNode mnode = ts_node_child(anode, mi);
                            if (!ts_node_is_named(mnode))
                                continue;
                            const char *mType = ts_node_type(mnode);
                            if (StrEq(mType, "annotation") || StrEq(mType, "marker_annotation"))
                            {
                                node_range ar = NodeRange(mnode);
                                if (annotation.size > 0)
                                    InlineVec::Append(annotation, (uint8_t)' ');
                                for (uint32_t ak = ar.startByte; ak < ar.endByte && annotation.size < 63; ++ak)
                                    InlineVec::Append(annotation, source.data[ak]);
                            }
                        }
                    }
                    else if (StrEq(aType, "annotation") || StrEq(aType, "marker_annotation"))
                    {
                        node_range ar = NodeRange(anode);
                        if (annotation.size > 0)
                            InlineVec::Append(annotation, (uint8_t)' ');
                        for (uint32_t ak = ar.startByte; ak < ar.endByte && annotation.size < 63; ++ak)
                            InlineVec::Append(annotation, source.data[ak]);
                    }
                }
            }

            // Deduplicate: Java tree-sitter may expose the same block via
            // both the declaration and its body node.
            bool duplicate = false;
            for (uint64_t j = 0; j < result.size; ++j)
            {
                block_info &existing = result.data.data[j];
                if (existing.startLine == r.startRow && existing.endLine == r.endRow &&
                    existing.type.size == typeName.size &&
                    MemEq(existing.type.data.data, typeName.data.data, typeName.size))
                {
                    // Same position: keep the one with a better name (non-empty).
                    if (name.size > 0 && existing.name.size == 0)
                        existing.name = name;
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
            {
                InlineVec::Append(result, block_info{
                                              .type = typeName,
                                              .name = name,
                                              .signature = signature,
                                              .annotation = annotation,
                                              .startLine = r.startRow,
                                              .endLine = r.endRow,
                                              .startByte = r.startByte,
                                              .endByte = r.endByte,
                                          });
            }
        }
    }
}

// ─── ListBlocks ─────────────────────────────────────────────────────────────

auto ListBlocks(byteview filePath, region_alloc &alloc) -> inline_vec<block_info, 256>
{
    inline_vec<block_info, 256> result{};

    if (!IsRegularFile(filePath, alloc))
    {
        LOG("ERROR: not a regular file '%.*s'", (int)filePath.size, filePath.data);
        return result;
    }

    file_handle fh = FileOpen(filePath, FileOpenMode::Read);
    if (!FileValid(fh))
    {
        LOG("ERROR: cannot open '%.*s'", (int)filePath.size, filePath.data);
        return result;
    }
    byteview source = FileReadFully(alloc, fh);
    FileClose(fh);

    source_language lang = DetectLanguage(filePath);
    const TSLanguage *grammar = GrammarForLanguage(lang);
    if (!grammar)
    {
        LOG("ERROR: unsupported file type for '%.*s'", (int)filePath.size, filePath.data);
        return result;
    }
    TSTree *tree = ParseSource(source, grammar);
    TSNode root = ts_tree_root_node(tree);

    CollectBlockNodes(root, result, 0, true, source, lang);

    TreeDelete(tree);
    return result;
}

// ─── ReadBlock ──────────────────────────────────────────────────────────────

auto ReadBlock(byteview filePath, uint32_t line, region_alloc &alloc, bool raw) -> read_block_info
{
    read_block_info result{};

    block_loc bl = LocateBlock(filePath, line, alloc);
    if (!bl.tree)
        return result;

    byteview source = bl.source;

    // Build the structural path by walking up through containers
    inline_string<256> path{};
    {
        auto blockName = NodeName(bl.block, source);
        if (blockName.size > 0)
        {
            for (uint32_t i = 0; i < blockName.size; ++i)
                InlineVec::Append(path, blockName.data[i]);
        }

        TSNode parent = ts_node_parent(bl.block);
        while (!ts_node_is_null(parent))
        {
            const char *ptype = ts_node_type(parent);
            if (StrEq(ptype, "struct_specifier") || StrEq(ptype, "class_specifier") ||
                StrEq(ptype, "class_declaration") || StrEq(ptype, "namespace_definition") ||
                StrEq(ptype, "interface_declaration") || StrEq(ptype, "enum_declaration"))
            {
                auto parentName = NodeName(parent, source);
                if (parentName.size > 0)
                {
                    inline_string<256> newPath{};
                    for (uint32_t i = 0; i < parentName.size; ++i)
                        InlineVec::Append(newPath, parentName.data[i]);
                    InlineVec::Append(newPath, (uint8_t)':');
                    InlineVec::Append(newPath, (uint8_t)':');
                    for (uint32_t i = 0; i < path.size; ++i)
                        InlineVec::Append(newPath, path.data[i]);
                    path = newPath;
                }
            }
            parent = ts_node_parent(parent);
        }
    }

    // Fill result metadata
    while (*bl.type && result.type.size < 127)
    {
        InlineVec::Append(result.type, (uint8_t)*bl.type);
        ++bl.type;
    }
    result.path = path;
    result.startLine = bl.range.startRow;
    result.endLine = bl.range.endRow;
    result.startByte = bl.range.startByte;
    result.endByte = bl.range.endByte;

    // Round to line boundaries: read always outputs whole lines.
    uint32_t lineStartByte = bl.range.startByte;
    while (lineStartByte > 0 && source.data[lineStartByte - 1] != '\n')
        --lineStartByte;
    uint32_t lineEndByte = bl.range.endByte;
    while (lineEndByte < source.size && source.data[lineEndByte] != '\n')
        ++lineEndByte;
    if (lineEndByte < source.size)
        ++lineEndByte; // include the newline
    uint32_t blockLen = lineEndByte - lineStartByte;
    byteview blockText{source.data + lineStartByte, blockLen};

    if (raw)
    {
        span<uint8_t> textBuf = RegionAlloc::AllocArray<uint8_t>(alloc, blockText.size);
        MemCpy(textBuf.data, blockText.data, blockText.size);
        result.text = byteview{textBuf.data, blockText.size};
        TreeDelete(bl.tree);
        return result;
    }

    // Extract and un-indent the block text.
    uint32_t firstLineIndent = bl.range.startByte - lineStartByte;
    uint32_t minIndent = firstLineIndent;
    {
        uint32_t pos = 0;
        while (pos < blockText.size)
        {
            uint32_t lineIndent = 0;
            while (pos + lineIndent < blockText.size &&
                   (blockText.data[pos + lineIndent] == ' ' || blockText.data[pos + lineIndent] == '\t'))
                ++lineIndent;

            uint32_t contentStart = pos + lineIndent;
            bool hasContent = false;
            while (contentStart < blockText.size && blockText.data[contentStart] != '\n')
            {
                if (blockText.data[contentStart] != ' ' && blockText.data[contentStart] != '\t')
                {
                    hasContent = true;
                    break;
                }
                ++contentStart;
            }

            if (hasContent)
            {
                uint32_t effectiveIndent = (pos == 0) ? firstLineIndent : lineIndent;
                if (effectiveIndent < minIndent)
                    minIndent = effectiveIndent;
            }

            while (pos < blockText.size && blockText.data[pos] != '\n')
                ++pos;
            if (pos < blockText.size)
                ++pos;
        }
    }
    if (minIndent == 0xFFFFFFFF)
        minIndent = 0;

    inline_vec<uint8_t, 65536> unindented{};
    {
        uint32_t pos = 0;
        while (pos < blockText.size)
        {
            uint32_t skipped = 0;
            while (pos < blockText.size && skipped < minIndent &&
                   (blockText.data[pos] == ' ' || blockText.data[pos] == '\t'))
            {
                ++pos;
                ++skipped;
            }

            while (pos < blockText.size && blockText.data[pos] != '\n')
            {
                InlineVec::Append(unindented, blockText.data[pos]);
                ++pos;
            }
            if (pos < blockText.size && blockText.data[pos] == '\n')
            {
                InlineVec::Append(unindented, (uint8_t)'\n');
                ++pos;
            }
        }
    }

    span<uint8_t> textBuf = RegionAlloc::AllocArray<uint8_t>(alloc, unindented.size);
    MemCpy(textBuf.data, unindented.data.data, unindented.size);
    result.text = byteview{textBuf.data, unindented.size};

    TreeDelete(bl.tree);
    return result;
}

} // namespace nyla
