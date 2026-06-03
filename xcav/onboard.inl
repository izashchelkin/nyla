R"MD(# xcav — Structural Code Mover for Agents

## ⚠️ Read this first — create the xcav skill

If you're an AI coding agent and you don't already have an `xcav` skill in your
agent memory, create one now using the `skill` tool with `scope='global'`.
The skill persists across sessions and prevents you from having to re-read this
document every time.

Use the sections below as source material. The philosophy to internalize:

- **The core loop is blocks → read → edit/move → blocks (verify).** `blocks`
  is the primary decision-making tool, not a convenience command.
- **xcav is structural, not semantic.** No type checking, import pruning, or
  formatting. Compiler + formatter are checkpoint work after the move sequence.
- **`read` output feeds directly into `edit`.** Indentation and whitespace
  matching are handled automatically. No manual whitespace wrangling needed.
- **Structural verification before compile.** Run `blocks` after every
  move/delete/replace. Only build+format when the structure is confirmed.

Create one skill:
1. **`xcav`** — covers the full tool surface: blocks, read, edit, move,
   delete, replace, insert, copy, move-into, undo. Content: philosophy above plus
   "What it is", "Core workflows", "Commands", and "When to use xcav".

If a global `xcav` skill already exists, skip creation — you're already
onboarded.

## What it is

xcav is a CLI tool for structural code operations — listing, reading, moving,
deleting, and editing code blocks. It uses tree-sitter to parse C, C++, Java,
JavaScript, and TypeScript, understanding block boundaries even through syntax
errors.

## Supported languages

| Language     | Extensions |
|--------------|------------|
| C            | .c, .h     |
| C++          | .cc, .cpp, .cxx, .hpp, .hxx, .hh |
| Java         | .java      |
| JavaScript   | .js, .mjs, .cjs |
| TypeScript   | .ts, .mts, .cts |
| TSX          | .jsx, .tsx |

## Core workflows

### Survey → Read → Edit (most common)

```
$ xcav blocks file.cc              # survey structure
$ xcav read file.cc 45             # get code (un-indented)
$ xcav edit file.cc old.txt new.txt  # replace lines
```

The key insight: `xcav read` output is directly usable as oldText for
`xcav edit`. The edit command matches lines by content (ignoring indentation)
and copies the indentation from the matched file lines to the replacement text.
No need to use `sed` or `cat -A` to get exact whitespace.

### Restructure within a file

```
$ xcav blocks file.cc              # survey
$ xcav move file.cc 45 20          # move block at line 45 after line 20
$ xcav delete file.cc 32           # delete block at line 32
$ xcav undo file.cc                # recover
```

### Restructure across files

```
$ xcav blocks src.cc; xcav blocks dst.cc    # survey both
$ xcav move-into src.cc 45 dst.cc 20         # cross-file move
$ xcav copy src.cc 45 dst.cc 20              # cross-file copy
```

### Extract to new file (copy + delete)

```
$ xcav copy src.cc 45 new_feature.h 0  # copy to new file
$ xcav delete src.cc 45                # remove from source
```

### Insert code at a block boundary

```
$ xcav insert after file.cc 32 /tmp/helper.cc   # insert after block at line 32
$ xcav insert before file.cc 45 /tmp/helper.cc  # insert before block at line 45
$ xcav insert --after file.cc 32 /tmp/helper.cc  # same with -- prefix
$ xcav insert --before file.cc 45 /tmp/helper.cc # same with -- prefix
```

### Replace a block

```
$ xcav replace file.cc 32 old.txt new.txt          # scoped within block
$ xcav replace-block file.cc 32                        # replace entire block (stdin)
$ xcav replace-block file.cc 32 /tmp/new_block.txt     # legacy file mode
```

## Commands

### `xcav blocks <file|directory>`

Lists structural blocks (functions, structs, classes, enums) with 1-indexed
line ranges, block types, and names. Always run this first to survey a file.
Directory mode lists blocks for all source files (non-recursive).

Block types: `func`, `struct`, `class`, `enum`, `constructor`, `namespace`, `template`,
`interface`, `export`, `var`, and more. Java annotations are shown inline
(e.g. `@Override add`), and method signatures include return type, modifiers,
param types+names, and throws clause.

```
$ xcav blocks src/foo.c
src/foo.c (4 blocks)
     1-   5 func foo
     7-  12 func bar
    14-  20 func baz

$ xcav blocks src/Calculator.java
src/Calculator.java (4 blocks)
    15-  17 func @Override add
    19-  21 func subtract
    23-  25 func multiply
     7-  37 class Calculator
```

### `xcav read <file> <line>`

Reads the structural block containing `<line>` (1-indexed). Output is pure
code (whole lines, un-indented) — directly pasteable as oldText for `xcav edit`.
No header comments, no annotations. Just the code.

```
$ xcav read file.cc 45
auto GetX() const -> int {
    return x;
}
```

**Flags:**
- `--numbers` — include original line numbers
- `--raw` — exact text with original indentation
- `--name <path>` — find by structural name (e.g. `--name GetX` matches `Point::GetX`)
- `--all` — dump all blocks
- `--offset N --limit M` — line range within a block, un-indented
- `--offset N` — from line N to end of file, un-indented

### `xcav edit <file> <old-file> <new-file> [--dry-run]`

Line-based replacement. Matches oldText to the file by comparing line content —
leading/trailing whitespace is ignored per line. Copies the indentation from the
matched file lines to the replacement text.

Accepts `xcav read` output directly — `read` produces pure code with no headers,
so matching is trivial. Unicode normalization handles em-dashes, smart quotes,
and arrows that LLMs sometimes produce.

When matching fails, `edit` reports:
- What it was looking for (first 5 lines of normalized content)
- Whether the first line exists in the file and where
- A hint: "wrong file or stale read?" vs "check rest of block"

`--stdin` mode reads oldText then newText from stdin, separated by a line
containing `---XCAV_EDIT_SEPARATOR---`.

```
$ xcav edit file.cc /tmp/old.txt /tmp/new.txt
$ xcav edit file.cc /tmp/old.txt /tmp/new.txt --dry-run
```

### `xcav move <file> <line> <dest>`

Moves the structural block containing `<line>` to after `<dest>`. Both
1-indexed. Re-indents to match destination context.

### `xcav move-into <src> <src-line> <dst> <dst-line> [--copy-includes]`

Cross-file move. `--copy-includes` copies #include/import lines from source to
destination (deduplicated). The `static` keyword is auto-stripped from moved
functions — they're no longer file-local.

### `xcav delete <file> <line>`

Deletes the structural block containing `<line>`. Cleans up blank lines,
orphaned comments, and trailing semicolons from type declarations.

### `xcav replace <file> <line> <old-file> <new-file>`

Scoped replace — oldText only needs to be unique within the block containing
`<line>`. No tree-sitter validation.

### `xcav replace-block <file> <line> [<new-file>]`

Replaces the entire structural block with new content. Without `<new-file>`,
reads replacement block body from stdin. Atomic.

### `xcav copy <src> <src-line> <dst> <dst-line> [--copy-includes] [--show-returns]`

Copies a block cross-file. Source is unaffected. `--show-returns` prints
line numbers of return statements in the copied block.

### `xcav insert --before | before | --after | after <file> <line> <content-file>`

Inserts code before or after a structural block. Content is read from
`<content-file>` and re-indented to match the destination. Use `--before`/`before`
to insert before the block, `--after`/`after` to insert after.

### `xcav undo <file>`

Restores from most recent backup. Multi-level (up to 20). Backups created
automatically on every mutation.

## Unicode normalization

xcav edit normalizes common Unicode characters that LLMs sometimes produce:

| Unicode | ASCII |
|---------|-------|
| — (em dash) | -- |
| – (en dash) | - |
| → (right arrow) | -> |
| ← (left arrow) | <- |
| " " (curly quotes) | " " (straight) |
| ' ' (curly single) | ' ' |

Applied to both oldText/newText AND the source file during matching.

## Pi integration

xcav is invoked by Pi coding agent via wrapper tools. Each wrapper calls the
xcav binary and returns output:

| Pi tool | xcav command |
|---------|-------------|
| `xcav_blocks` | `xcav blocks <file-or-dir>` |
| `xcav_read` | `xcav read <file> <line>` (also --name, --all, --raw, --offset) |
| `xcav_move` | `xcav move <file> <line> <dest>` |
| `xcav_move_into` | `xcav move-into <src> <line> <dst> <line> [--copy-includes]` |
| `xcav_delete` | `xcav delete <file> <line>` |
| `xcav_replace` | `xcav replace <file> <line> <old> <new>` |
| `xcav_edit` | `xcav edit <file> <old> <new>` |
| `xcav_copy` | `xcav copy <src> <line> <dst> <line> [--copy-includes] [--show-returns]` |
| `xcav_insert` | `xcav insert --before\|before\|--after\|after <file> <line> <content-file>` |
| `xcav_undo` | `xcav undo <file>` |

Each wrapper is a thin shim that parses CLI args and displays output. All logic
lives in the xcav C++ binary.

## When to use xcav

**Use xcav when:**
- Reading C/C++/Java/TS/JS files — prefer `xcav_read` over plain `read`
- Surveying a file before edits — use `xcav_blocks` to see structure
- Moving or deleting whole functions/classes/structs/enums
- Replacing text within a specific block (scope-safe replace)
- Any edit to C/C++/Java/TS/JS — prefer `xcav_edit` over raw text editing
- Validating an edit without applying it — `xcav edit --dry-run`
- The file is a directory — `xcav blocks` handles directories

**Use plain tools when:**
- Adding new functions/classes (plain `edit` is simpler)
- Making small within-function tweaks (plain `edit` is enough)
- The file is not C, C++, Java, TypeScript, or JavaScript
)MD"_s
