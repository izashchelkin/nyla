---
name: "xcav"
description: "When reading or editing C, C++, Java, TypeScript, or JavaScript files: intercepts the default plain read/edit habit and redirects to xcav tools. Covers reading, editing, structural survey, block-level moves, deletes, cross-file restructuring, and usage analysis."
version: 4
created: "2026-06-02"
updated: "2026-06-02"
---
## When to Use
Whenever you touch a C, C++, Java, TypeScript, or JavaScript file. Never use plain `read` or plain `edit` for these languages — xcav tools give you structural awareness that plain tools lack. The core rule: blocks first, then read, then edit. Always survey before cutting.

Do NOT use xcav for: plain text/config files, adding brand-new functions (plain edit is simpler), or small within-function tweaks where xcav auto-indentation might complicate follow-up edits.

## Philosophy
- **The core loop is blocks → read → edit/move → blocks (verify).** `blocks` is the primary decision-making tool, not a convenience command.
- **xcav is structural, not semantic.** It won't tell you whether a method is required by an interface, whether removing an override breaks a contract, or whether a forwarder delegates correctly. Those are compiler/formatter questions. Use xcav to find and reshape the code; use the build to verify correctness.
- **`read` output feeds directly into `edit`.** Indentation and whitespace matching are handled automatically. No manual whitespace wrangling needed.
- **Structural verification before compile.** Run `blocks` after every move/delete/replace. Only build+format when the structure is confirmed.

## Supported languages
| Language     | Extensions |
|--------------|------------|
| C            | .c, .h     |
| C++          | .cc, .cpp, .cxx, .hpp, .hxx, .hh |
| Java         | .java      |
| JavaScript   | .js, .mjs, .cjs |
| TypeScript   | .ts, .mts, .cts |
| TSX          | .jsx, .tsx |

## Commands

### `xcav blocks <file|directory>`
Lists structural blocks (functions, structs, classes, enums) with 1-indexed line ranges, block types, and names. Always run this first to survey a file. Directory mode lists blocks for all source files (non-recursive).

Block types: `func`, `struct`, `class`, `enum`, `constructor`, `namespace`, `template`, `interface`, `export`, `var`, and more. Java annotations are shown inline (e.g. `@Override add`), and method signatures include return type, modifiers, param types+names, and throws clause.

### `xcav read <file> <line>`
Reads the structural block containing `<line>` (1-indexed). Output is pure code (whole lines, un-indented) — directly pasteable as oldText for `xcav edit`. No header comments, no annotations. Just the code.

**Flags:** `--numbers` (line numbers), `--raw` (original indentation), `--name <path>` (structural name lookup), `--all` (dump all blocks), `--offset N --limit M` (line range), `--offset N` (from line N to end).

### `xcav edit <file> <old-file> <new-file> [--dry-run]`
Line-based replacement. Matches oldText to the file by comparing line content — leading/trailing whitespace is ignored per line. Copies the indentation from the matched file lines to the replacement text. Accepts `xcav read` output directly. Unicode normalization handles em-dashes, smart quotes, and arrows. `--stdin` mode reads oldText then newText from stdin separated by `---XCAV_EDIT_SEPARATOR---`.

When matching fails, `edit` reports what it was looking for, whether the first line exists, and a hint ("wrong file or stale read?" vs "check rest of block").

### `xcav move <file> <line> <dest>`
Moves the structural block containing `<line>` to after `<dest>`. Both 1-indexed. Re-indents to match destination context.

### `xcav move-into <src> <src-line> <dst> <dst-line> [--copy-includes]`
Cross-file move. `--copy-includes` copies #include/import lines from source to destination (deduplicated). The `static` keyword is auto-stripped from moved functions.

### `xcav delete <file> <line>`
Deletes the structural block containing `<line>`. Cleans up blank lines, orphaned comments, and trailing semicolons from type declarations.

### `xcav replace <file> <line> <old-file> <new-file>`
Scoped replace — oldText only needs to be unique within the block containing `<line>`. No tree-sitter validation.

### `xcav replace-block <file> <line> <new-file>`
Replaces the entire structural block with content from `<new-file>`. Atomic.

### `xcav copy <src> <src-line> <dst> <dst-line> [--copy-includes] [--show-returns]`
Copies a block cross-file. Source is unaffected. `--show-returns` prints line numbers of return statements.

### `xcav undo <file>`
Restores from most recent backup. Multi-level (up to 20). Backups created automatically on every mutation.

## Unicode normalization
xcav edit normalizes common Unicode characters: em dash→--, en dash→-, →→->, ←→<-, curly quotes→straight quotes. Applied to both oldText/newText AND the source file during matching.

## Pi integration
| Pi tool | xcav command |
|---------|-------------|
| `xcav_blocks` | `xcav blocks <file-or-dir>` |
| `xcav_read` | `xcav read <file> <line>` (also --name, --all, --raw, --offset) |
| `xcav_move` | `xcav move <file> <line> <dest>` |
| `xcav_move_into` | `xcav move-into <src> <line> <dst> <line> [--copy-includes]` |
| `xcav_delete` | `xcav delete <file> <line>` |
| `xcav_replace` | `xcav replace-block <file> <line> <new-file>` |
| `xcav_replace_scoped` | `xcav replace <file> <line> <old> <new>` (scoped to block) |
| `xcav_edit` | `xcav edit <file> <old> <new>` |
| `xcav_copy` | `xcav copy <src> <line> <dst> <line> [--copy-includes] [--show-returns]` |
| `xcav_undo` | `xcav undo <file>` |

Each wrapper is a thin shim. All logic lives in the xcav C++ binary.

## When to use xcav vs plain tools
**Use xcav when:** reading C/C++/Java/TS/JS files, surveying before edits, moving/deleting whole functions/classes/structs/enums, scoped-block replaces, validating edits with `--dry-run`, or the file is a directory.

**Use plain edit only when:** making multiple consecutive edits within the same function body where xcav_edit's auto-indentation would break subsequent line matches. For single-method replacements, wrapper removals, or body rewrites, use xcav_edit — even if the change is one line. Also use plain tools when the file is not a supported language.

## Workflow Example

Here is the full pipeline for replacing a forwarding method body — the most common pattern:

```
# Step 1: Survey
$ xcav_blocks MyManager.java
  42: func findById
  78: func deleteById

# Step 2: Read the wrapper you want to change
$ xcav_read MyManager.java 42
auto findById(String id) -> Optional<Entity> {
    return delegate.findById(id);
}

# Step 3: Edit — paste the read output directly as oldText
# In Pi: xcav_edit uses the same oldText/newText format as plain edit
oldText:
auto findById(String id) -> Optional<Entity> {
    return delegate.findById(id);
}
newText:
auto findById(String id) -> Optional<Entity> {
    return cache.getOrLoad(id, () -> delegate.findById(id));
}

# Step 4: Verify structure intact
$ xcav_blocks MyManager.java
```

## Procedure
1. Survey: `xcav_blocks <file>` — always run first. Lists every function/struct/class/enum with 1-indexed line numbers.
2. Read: `xcav_read <file> <line>` — pure code output, directly pasteable as oldText for `xcav_edit`.
3. Edit: `xcav_edit <file>` — line-based replacement. Use `--dry-run` to preview. For multiple edits within the same function, use plain edit (xcav_edit auto-indents, breaking subsequent matches).
4. Move: `xcav_move <file> <line> <destLine>` — both 1-indexed, re-indents.
5. Delete: `xcav_delete <file> <line>` — never batch without running xcav_blocks between calls.
6. Cross-file: `xcav_move_into` or `xcav_copy` — `--copy-includes` available.
7. Replace: `xcav_replace` (whole block) or `xcav_replace_scoped` (text within a block) — atomic, safe for last-block-in-namespace.
8. Verify: `xcav_blocks` after every mutation. Compile and format are separate checkpoint steps.
9. Undo: `xcav_undo <file>` — multi-level (up to 20).

## Pitfalls
- xcav_edit auto-indents on write. For multiple edits within the same function, use plain edit.
- xcav_blocks uses 1-indexed line numbers. Destinations must be block boundaries, not random lines.
- Never batch multiple xcav_delete calls on the same file without xcav_blocks between.
- xcav is structural, not semantic. Do type checking, import cleanup, and formatting separately.
- xcav_read output for enum/struct/class may include the trailing ; (tree-sitter rounds to line boundaries).
- Directory-level `xcav_blocks` is useful once to orient in unfamiliar code. Once you know the target file names, switch to per-file blocks. Directory scans on packages with many files produce noise, not signal.
- If you've read the same block 3+ times without editing it, you're stuck in survey mode. Either edit it now or move on.

## Verification
1. `xcav_blocks` shows expected structure after changes
2. Build succeeds (separate step after structural work is confirmed)
3. Format passes (separate step after structural work is confirmed)