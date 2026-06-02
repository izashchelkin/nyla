#include "xcav/inline.h"

#include "xcav/backup.h"
#include "xcav/block_query.h"
#include "xcav/text_util.h"
#include "xcav/tree_sitter_util.h"

#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"
#include "nyla/commons/stringparser.h"

#include <tree_sitter/api.h>

namespace nyla
{

// Recursively find a node type in a subtree
static auto FindNode(TSNode root, const char *typeName) -> TSNode
{
    inline_vec<TSNode, 128> stack{};
    InlineVec::Append(stack, root);
    while (stack.size > 0)
    {
        TSNode n = stack.data.data[stack.size - 1];
        --stack.size;
        if (StrEq(ts_node_type(n), typeName))
            return n;
        uint32_t nc = ts_node_child_count(n);
        for (uint32_t i = 0; i < nc; ++i)
            InlineVec::Append(stack, ts_node_child(n, i));
    }
    return {};
}

// Find function definition by name
static auto FindFunctionDef(TSTree *tree, byteview source, byteview funcName, TSNode *out) -> bool
{
    TSNode root = ts_tree_root_node(tree);
    inline_vec<TSNode, 128> stack{};
    uint32_t rc = ts_node_child_count(root);
    for (uint32_t i = 0; i < rc; ++i)
        InlineVec::Append(stack, ts_node_child(root, i));

    while (stack.size > 0)
    {
        TSNode n = stack.data.data[stack.size - 1];
        --stack.size;
        const char *t = ts_node_type(n);

        if (StrEq(t, "function_definition") || StrEq(t, "method_definition"))
        {
            TSNode decl = FindNode(n, "function_declarator");
            if (!ts_node_is_null(decl))
            {
                // Function name is direct child identifier of function_declarator
                uint32_t dc = ts_node_child_count(decl);
                for (uint32_t i = 0; i < dc; ++i)
                {
                    TSNode c = ts_node_child(decl, i);
                    if (StrEq(ts_node_type(c), "identifier"))
                    {
                        uint32_t s = ts_node_start_byte(c);
                        uint32_t e = ts_node_end_byte(c);
                        if (e - s == funcName.size &&
                            MemEq(source.data + s, funcName.data, funcName.size))
                        {
                            *out = n;
                            return true;
                        }
                        break;
                    }
                }
            }
        }

        if (StrEq(t, "class_declaration") || StrEq(t, "namespace_definition") ||
            StrEq(t, "translation_unit") || StrEq(t, "class_body"))
        {
            uint32_t nc = ts_node_child_count(n);
            for (uint32_t i = 0; i < nc; ++i)
                InlineVec::Append(stack, ts_node_child(n, i));
        }
    }
    return false;
}

auto InlineFunctionCall(byteview filePath, uint32_t callSiteLine, region_alloc &alloc) -> InlineResult
{
    // Locate the block (0-indexed line)
    block_loc bl = LocateBlock(filePath, callSiteLine - 1, alloc);
    if (!bl.tree)
        return {false, "no structural block at line"};

    byteview source = bl.source;

    // Find call_expression at the target row
    TSNode callNode = {};
    {
        uint32_t tr = callSiteLine - 1;
        inline_vec<TSNode, 128> stack{};
        InlineVec::Append(stack, bl.block);
        while (stack.size > 0)
        {
            TSNode n = stack.data.data[stack.size - 1];
            --stack.size;
            if (StrEq(ts_node_type(n), "call_expression"))
            {
                uint32_t sr = ts_node_start_point(n).row;
                uint32_t er = ts_node_end_point(n).row;
                if (tr >= sr && tr <= er)
                    callNode = n;
            }
            uint32_t nc = ts_node_child_count(n);
            for (uint32_t i = 0; i < nc; ++i)
                InlineVec::Append(stack, ts_node_child(n, i));
        }
    }

    if (ts_node_is_null(callNode))
    {
        TreeDelete(bl.tree);
        return {false, "no call expression at line"};
    }

    // Extract function name from call_expression
    inline_string<128> funcName{};
    {
        TSNode fn = ts_node_child(callNode, 0);
        if (ts_node_is_null(fn))
        {
            TreeDelete(bl.tree);
            return {false, "malformed call expression"};
        }
        if (StrEq(ts_node_type(fn), "identifier"))
        {
            for (uint32_t i = ts_node_start_byte(fn); i < ts_node_end_byte(fn); ++i)
                InlineVec::Append(funcName, source.data[i]);
        }
        else
        {
            TreeDelete(bl.tree);
            return {false, "unsupported call target (only simple identifiers)"};
        }
    }

    if (funcName.size == 0)
    {
        TreeDelete(bl.tree);
        return {false, "empty function name"};
    }

    // Find the function definition
    TSNode funcDef;
    if (!FindFunctionDef(bl.tree, source,
                         byteview{funcName.data.data, funcName.size}, &funcDef))
    {
        TreeDelete(bl.tree);
        return {false, "function definition not found"};
    }

    // Get body and parameters
    TSNode body = FindNode(funcDef, "compound_statement");
    if (ts_node_is_null(body))
        body = FindNode(funcDef, "block");
    if (ts_node_is_null(body))
    {
        TreeDelete(bl.tree);
        return {false, "function body not found"};
    }

    TSNode params = FindNode(funcDef, "parameter_list");

    // Check: is the body a simple "return expr;"?
    uint32_t stmtCount = 0;
    {
        uint32_t nc = ts_node_child_count(body);
        for (uint32_t i = 0; i < nc; ++i)
        {
            const char *ct = ts_node_type(ts_node_child(body, i));
            if (StrEq(ct, "return_statement") || StrEq(ct, "expression_statement") ||
                StrEq(ct, "if_statement"))
                ++stmtCount;
        }
    }

    if (stmtCount != 1)
    {
        TreeDelete(bl.tree);
        return {false, "function body has multiple statements — not yet supported"};
    }

    // Get the return expression
    TSNode retNode = FindNode(body, "return_statement");
    if (ts_node_is_null(retNode))
    {
        TreeDelete(bl.tree);
        return {false, "function body is not a simple return"};
    }

    // Get the expression (skip "return" and ";" children)
    TSNode exprNode = {};
    {
        uint32_t nc = ts_node_child_count(retNode);
        for (uint32_t i = 0; i < nc; ++i)
        {
            TSNode c = ts_node_child(retNode, i);
            const char *ct = ts_node_type(c);
            if (!StrEq(ct, "return") && !StrEq(ct, ";"))
            {
                exprNode = c;
                break;
            }
        }
    }

    if (ts_node_is_null(exprNode))
    {
        TreeDelete(bl.tree);
        return {false, "return has no expression"};
    }

    // Extract parameter names
    inline_vec<byteview, 8> paramNames{};
    if (!ts_node_is_null(params))
    {
        uint32_t pc = ts_node_child_count(params);
        for (uint32_t i = 0; i < pc; ++i)
        {
            TSNode id = FindNode(ts_node_child(params, i), "identifier");
            if (!ts_node_is_null(id))
                InlineVec::Append(paramNames,
                                  byteview{source.data + ts_node_start_byte(id),
                                           ts_node_end_byte(id) - ts_node_start_byte(id)});
        }
    }

    // Extract argument expressions from call
    inline_vec<byteview, 8> argExprs{};
    {
        uint32_t nc = ts_node_child_count(callNode);
        for (uint32_t i = 1; i < nc; ++i)
        {
            TSNode c = ts_node_child(callNode, i);
            if (StrEq(ts_node_type(c), "argument_list"))
            {
                uint32_t ac = ts_node_child_count(c);
                for (uint32_t j = 0; j < ac; ++j)
                {
                    TSNode a = ts_node_child(c, j);
                    if (!StrEq(ts_node_type(a), ",") && !StrEq(ts_node_type(a), "(") &&
                        !StrEq(ts_node_type(a), ")"))
                        InlineVec::Append(argExprs,
                                          byteview{source.data + ts_node_start_byte(a),
                                                   ts_node_end_byte(a) - ts_node_start_byte(a)});
                }
                break;
            }
        }
    }

    // Generate inlined expression: walk the return expression, substitute params→args
    inline_vec<uint8_t, 16384> inlined{};
    {
        uint32_t es = ts_node_start_byte(exprNode);
        uint32_t ee = ts_node_end_byte(exprNode);
        uint32_t pos = es;
        while (pos < ee)
        {
            uint8_t c = source.data[pos];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
            {
                uint32_t idStart = pos;
                while (pos < ee)
                {
                    uint8_t c2 = source.data[pos];
                    if ((c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z') ||
                        (c2 >= '0' && c2 <= '9') || c2 == '_')
                        ++pos;
                    else
                        break;
                }
                uint32_t idLen = pos - idStart;
                byteview ident = {source.data + idStart, idLen};

                bool sub = false;
                for (uint32_t pi = 0; pi < paramNames.size && pi < argExprs.size; ++pi)
                {
                    if (paramNames.data.data[pi].size == idLen &&
                        MemEq(paramNames.data.data[pi].data, ident.data, idLen))
                    {
                        byteview arg = argExprs.data.data[pi];
                        for (uint32_t k = 0; k < arg.size; ++k)
                            InlineVec::Append(inlined, arg.data[k]);
                        sub = true;
                        break;
                    }
                }
                if (!sub)
                {
                    for (uint32_t k = idStart; k < pos; ++k)
                        InlineVec::Append(inlined, source.data[k]);
                }
            }
            else
            {
                InlineVec::Append(inlined, source.data[pos]);
                ++pos;
            }
        }
    }

    // Find the enclosing statement (for block-wrapping the inline)
    TSNode stmtNode = callNode;
    {
        TSNode p = ts_node_parent(callNode);
        while (!ts_node_is_null(p))
        {
            const char *pt = ts_node_type(p);
            if (StrEq(pt, "expression_statement") || StrEq(pt, "declaration") ||
                StrEq(pt, "return_statement"))
            {
                stmtNode = p;
                break;
            }
            if (StrEq(pt, "compound_statement") || StrEq(pt, "block") ||
                StrEq(pt, "function_definition") || StrEq(pt, "translation_unit"))
                break;
            p = ts_node_parent(p);
        }
    }

    uint32_t stmtStart = ts_node_start_byte(stmtNode);
    uint32_t stmtEnd = ts_node_end_byte(stmtNode);
    uint32_t callStart = ts_node_start_byte(callNode);
    uint32_t callEnd = ts_node_end_byte(callNode);

    // Wrap the statement in a block, with the call replaced by the inlined expression.
    // { stmt_before_call INLINED stmt_after_call }
    // This scopes any declarations and prevents operator precedence issues.
    // NOTE: return values are NOT captured -- the agent must fix the result manually
    // if the call site expects one (e.g. int x = foo() becomes { int x = EXPR; }).
    SaveBackup(filePath, alloc);

    uint64_t blockOverhead = 3; // "{ " and " }"
    uint64_t total = source.size - (stmtEnd - stmtStart) + (callEnd - callStart)  // remove call, add back
                      - (callEnd - callStart) + inlined.size                         // replace call with inlined
                      + blockOverhead;
    // Simplified: source - stmt + (stmtPrefix) + inlined + (stmtSuffix) + block wrapper
    // stmtPrefix = callStart - stmtStart bytes of the statement before the call
    // stmtSuffix = stmtEnd - callEnd bytes of the statement after the call
    uint64_t stmtPrefixLen = callStart - stmtStart;
    uint64_t stmtSuffixLen = stmtEnd - callEnd;
    total = source.size - (stmtEnd - stmtStart) + stmtPrefixLen + inlined.size + stmtSuffixLen + blockOverhead;

    span<uint8_t> out = RegionAlloc::AllocArray<uint8_t>(alloc, total);
    uint64_t op = 0;

    // Everything before the statement
    MemCpy(out.data, source.data, stmtStart);
    op += stmtStart;

    // Opening brace
    out.data[op++] = (uint8_t)'{';
    out.data[op++] = (uint8_t)' ';

    // Statement prefix (before the call)
    MemCpy(out.data + op, source.data + stmtStart, stmtPrefixLen);
    op += stmtPrefixLen;

    // Inlined expression
    MemCpy(out.data + op, inlined.data.data, inlined.size);
    op += inlined.size;

    // Statement suffix (after the call)
    MemCpy(out.data + op, source.data + callEnd, stmtSuffixLen);
    op += stmtSuffixLen;

    // Closing brace
    out.data[op++] = (uint8_t)' ';
    out.data[op++] = (uint8_t)'}';

    // Everything after the statement
    MemCpy(out.data + op, source.data + stmtEnd, source.size - stmtEnd);

    file_handle fh = FileOpen(filePath, FileOpenMode::Write);
    if (!FileValid(fh))
    {
        TreeDelete(bl.tree);
        return {false, "cannot write file"};
    }
    FileWrite(fh, (uint32_t)total, out.data);
    FileClose(fh);

    TreeDelete(bl.tree);
    return {true, nullptr};
}

} // namespace nyla
