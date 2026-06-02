R"MD(# xcav — Structural Code Mover

## What it is

xcav is a CLI tool for structural code operations — listing, reading, moving,
deleting, editing, extracting, and tidying code blocks. It uses tree-sitter to
parse C, C++, Java, JavaScript, and TypeScript, understanding block boundaries
even through syntax errors. It handles indentation, block extraction, validation,
and Unicode normalization automatically.

Build: `cmake --build build/linux-debug --target xcav`
Binary: `build/linux-debug/bin/xcav` (installed to `/usr/local/bin/xcav`)

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

Lists structural blocks (functions, structs, classes, enums, includes,
declarations) with 1-indexed line ranges and block names. Always run this first
to survey a file. If the path is a directory, lists blocks for all source files
in that directory (non-recursive). Each file's output is preceded by a header
line with the filename.

```
$ xcav blocks src/foo.c
src/foo.c (4 blocks)
  1-5   func    foo
  7-12  func    bar
  14-20 func    baz
  2-3   include stdio.h

$ xcav blocks src/
src/foo.c (4 blocks)
  1-5   func    foo
  7-12  func    bar
src/bar.c (3 blocks)
  3-8   func    baz
  1-2   include stdlib.h
```

Block types: `func` (function/method), `struct`, `class`, `enum`, `union`,
`decl` (declaration), `include`, `import`, `namespace`, `template`,
`interface` (Java/TS), plus TS/JS variants like `export`, `variable`, `lexical`.

Java example — methods are nested under their class with structural paths:

```
$ xcav blocks src/Calculator.java
src/Calculator.java (4 blocks)
   4-  6 func    Calculator::add
   8- 10 func    Calculator::subtract
  12- 14 func    Calculator::multiply
   2- 15 class   Calculator
```

TypeScript example — interfaces, type aliases, and exports:

```
$ xcav blocks src/geometry.ts
src/geometry.ts (5 blocks)
   1- 14 func        distance
  16- 25 interface   Point
  27- 28 type        Color
  30- 35 func        draw
  30- 35 export      draw
```

### `xcav read <file> <line>`

Reads the structural block containing `<line>` (1-indexed). Output shows a header
comment with the structural path (e.g. `Point::GetX`) and the block text,
un-indented so the least-indented line sits at column 0.

```
$ xcav read file.cc 45
// Point::GetX (function_definition, lines 7-9)
auto GetX() const -> int {
    return x;
}
```

### `xcav read <file> --offset <N> [--limit <M>] [--numbers]`

Reads a line range, locating the enclosing block and un-indenting the visible
slice so the least-indented line in the range becomes column 0. Relative
indentation within the slice is preserved.

```
$ xcav read file.cc --offset 1200 --limit 10
if (!active)
{
    xcb_window_t win = cm->window;
    auto *idx = FindIndex(win);
```

### `xcav read <file> --name <path>`

Finds a block by structural name. Supports suffix matching (`GetX` matches
`Point::GetX`).

```
$ xcav read file.cc --name foo
```

### `xcav read <file> --all [--numbers]`

Dumps all structural blocks with un-indented code. Use `--numbers` to include
original line numbers.

### `xcav read <file> --raw`

Disables un-indenting — outputs the block's exact text with original indentation.
Useful when you need to see the exact whitespace for matching in edits.

```
$ xcav read file.cc 45 --raw
// Point::GetX (function_definition, lines 7-9)
    auto GetX() const -> int {
        return x;
    }
```

### `xcav move <file> <line> <dest>`

Moves the structural block containing `<line>` to after `<dest>`. Both 1-indexed.
The block is re-indented to match the destination context. Destination must be a
block boundary (closing brace, file start/end).

```
$ xcav move src/foo.c 14 5
$ xcav move Calculator.java 4 14   # move Java method within class
```

### `xcav move-into <src> <src-line> <dst> <dst-line> [--copy-includes]`

Cross-file move. `--copy-includes` copies #include/import lines from source to
destination (deduplicated). Works across languages — moves Java/TS/JS blocks
between files.

```
$ xcav move-into a.cc 45 b.cc 20 --copy-includes
$ xcav move-into A.java 10 B.java 5
```

### `xcav extract <src> <line> <new-file>`

Move the structural block at `<line>` to a new file.
- **C/C++**: adds `#pragma once`, namespace wrapping, copies `#include`s.
- **Java/TS/JS**: copies `import` lines. No pragma/namespace generation.
- A `#include "new-file"` is added to C/C++ sources. Java/TS/JS sources are
  not modified with auto-imports.

```
$ xcav extract file.cc 45 new_feature.h
$ xcav extract App.java 10 SubFeature.java
```

### `xcav delete <file> <line>`

Deletes the structural block containing `<line>` (1-indexed). Cleans up
surrounding blank lines and orphaned comments (block-level comments above the
deleted block, but not file-level header comments). Backups are saved
automatically.

```
$ xcav delete file.cc 32
```

### `xcav edit <file> <old-file> <new-file> [--dry-run] [--force] [--diff]`

Safe edit with tree-sitter validation. Replaces oldText with newText, then:
- Normalizes Unicode punctuation (em-dash → `--`, arrows → ASCII) in both texts
- Auto-re-indents to match surrounding code
- Whitespace-insensitive matching (leading/trailing whitespace ignored per line)
- Strips trailing whitespace, normalizes tabs→spaces, collapses blank lines
- Validates with tree-sitter — rejects if new syntax errors appear
- Tolerates pre-existing syntax errors

For agents, use `--stdin` mode:
```
$ { echo "oldText"; echo "---XCAV_EDIT_SEPARATOR---"; echo "newText"; } | xcav edit file.cc --stdin
```

`--dry-run` validates and reports the match position, but does not write to disk.
`--diff` prints a unified diff (old text with `-`, new text with `+`) to stdout.
`--force` skips tree-sitter validation and re-indentation entirely.

### `xcav replace <file> <line> <old-file> <new-file>`

Like `edit` but scoped to the structural block containing `<line>`. oldText
doesn't need to be globally unique — only unique within the block. No tree-sitter
validation. Unicode normalization is applied.

```
$ xcav replace file.cc 32 /tmp/old.txt /tmp/new.txt
```

### `xcav undo <file>`

Restores `<file>` from the most recent backup in `.xcav_backups/`. Backups are
created automatically before every move/delete/edit/replace/tidy/extract.
`.xcav_backups/.gitignore` is created automatically so backup payloads stay out
of commits.
Supports multiple undo levels (one backup per operation version).

```
$ xcav undo file.cc
```

### `xcav tidy <file>`

Re-indents every structural block in the file based on tree-sitter nesting depth:
namespaces at column 0, struct/class/enum members at +4, function bodies at +8,
control flow blocks using brace counting. Also cleans up trailing whitespace,
tab→space normalization, and blank line collapsing.

```
$ xcav tidy file.cc
```

### `xcav inline <file> <line>`

Inlines the function called at `<line>` into the call site. The call is replaced
with the function's body wrapped in a `{ }` block. Parameters are substituted
with arguments.

```
$ xcav inline file.c 12
// Before: int32_t total = Area(w, h) + 10;
// After:  { int32_t total = w * h + 10; }
```

**Currently supports**: simple single-return functions (`return expr;`).
Multi-statement bodies (with early returns, if/else, loops) are rejected with
a clear error.

**Block wrapping**: the inlined code is always wrapped in `{ }` to scope
any declarations. **Return values are NOT captured** — if the call site expects
a return value (e.g. `int x = foo()`), the agent must manually fix the result
assignment after inlining.

### `xcav help` / `xcav onboard`

Prints help text or this onboarding guide.

## When to use xcav

**Use xcav when:**
- Reading C/C++/Java/TS/JS files — prefer `xcav_read` over plain `read`
- Surveying a file before edits — use `xcav_blocks` to see structure
- Surveying a directory — `xcav blocks <dir>` lists blocks for all files
- Moving a function/method/class/struct/enum to a different location
- Extracting a block to a new file
- Deleting a whole function/class/struct/enum
- Replacing text within a specific block (scope-safe replace)
- Inlining a simple single-return function — `xcav inline <file> <line>`
- Auto-re-indenting a file — `xcav tidy`
- Any edit to C/C++/Java/TS/JS — prefer `xcav edit` over raw text editing
- Validating an edit without applying it — `xcav edit --dry-run`

**Do NOT use xcav when:**
- The destination line is inside the block being moved
- The file is not C, C++, Java, TypeScript, or JavaScript
- Making small within-function edits (plain `edit` is simpler)
- Adding new functions/classes (plain `edit` is fine for additions)
- The file is a directory — `xcav blocks` handles directories, other commands don't
- Using `xcav tidy` on Java — indentation may be incorrect; use IDE formatting instead
- Using `xcav extract` on Java inner methods — only top-level classes/methods extract cleanly
- Using `xcav inline` on multi-statement functions — only `return expr;` bodies are supported
- Relying on `xcav inline` to capture return values — the agent must fix result assignments manually

## Unicode normalization

xcav automatically normalizes common Unicode characters that LLMs sometimes
produce, converting them to ASCII equivalents before matching:

| Unicode | ASCII |
|---------|-------|
| — (em dash) | -- |
| – (en dash) | -- |
| → (right arrow) | -> |
| ← (left arrow) | <- |
| … (ellipsis) | ... |
| " " (curly quotes) | " " (straight quotes) |
| ' ' (curly single quotes) | ' ' |
| — (horizontal ellipsis) | --- |

This normalization is applied to both oldText/newText AND the source file during
matching. It prevents "oldText not found" failures caused by invisible Unicode
in LLM output — even when the file contains em-dashes or curly quotes.

## Workflow

1. Run `xcav blocks <file>` to see available blocks with names and line ranges
2. Use `xcav read <file> <line>` to inspect a block, or `--offset`/`--limit` for a slice
3. Use `xcav move` / `xcav delete` / `xcav edit` / `xcav replace` / `xcav inline` to modify
4. Use `xcav tidy` to clean up indentation if needed
5. Use `xcav undo` to recover from mistakes
6. Re-run `xcav blocks` to verify the result

## Pi integration

xcav is invoked by Pi coding agent via wrapper tools:
- `xcav_blocks` — calls `xcav blocks`
- `xcav_read` — calls `xcav read` (with --raw, --offset, --limit, --name, --all, --numbers)
- `xcav_move` — calls `xcav move`
- `xcav_move_into` — calls `xcav move-into`
- `xcav_delete` — calls `xcav delete`
- `xcav_replace` — calls `xcav replace`
- `xcav_undo` — calls `xcav undo`
- `xcav_tidy` — calls `xcav tidy`
- `xcav_extract` — calls `xcav extract`
- `xcav_inline` — calls `xcav inline`
- `xcav_edit` — calls `xcav edit`

Each wrapper is a thin TypeScript shim that parses CLI args and displays output.
All logic lives in the xcav C++ binary.

## Claude Code integration

xcav is a standalone CLI binary that can be integrated with Claude Code as a
set of custom tools. Since xcav handles block detection, indentation, validation,
and Unicode normalization, it replaces manual text editing for structural changes.

### Setup

1. Install xcav: `./scripts/install_xcav.sh` (copies to `/usr/local/bin/xcav`)
2. Configure Claude Code with tool definitions that wrap each xcav subcommand.
   Recommended tool set:

| Claude Code Tool | xcav Command |
|-----------------|--------------|
| `xcav_blocks` | `xcav blocks <file-or-dir>` |
| `xcav_read` | `xcav read <file> <line>` (also --name, --all, --raw, --offset) |
| `xcav_move` | `xcav move <file> <line> <dest>` |
| `xcav_move_into` | `xcav move-into <src> <line> <dst> <line> [--copy-includes]` |
| `xcav_delete` | `xcav delete <file> <line>` |
| `xcav_replace` | `xcav replace <file> <line> <old-file> <new-file>` |
| `xcav_edit` | `xcav edit <file> --stdin` (pipe oldText + newText via stdin) |
| `xcav_extract` | `xcav extract <src> <line> <new-file>` |
| `xcav_inline` | `xcav inline <file> <line>` |
| `xcav_undo` | `xcav undo <file>` |
| `xcav_tidy` | `xcav tidy <file>` |

3. Each tool definition should:
   - Run `xcav <subcommand> ...` in the project root
   - Capture stdout/stderr and return it to the LLM
   - The LLM uses `xcav_blocks` first to survey, then `xcav_read` to inspect,
     then mutation commands (`move`/`delete`/`edit`/`extract`) to modify

### xcav_edit stdin format

```
{oldText}
---XCAV_EDIT_SEPARATOR---
{newText}
```

Example: pipe to `xcav edit file.java --stdin`

### Why use xcav with Claude Code

- Tree-sitter validates all edits — catches syntax errors before they're committed
- Structural block operations (move/delete/extract) are atomic — no manual cut-paste
- Unicode normalization prevents "oldText not found" failures from LLM unicode output
- Multi-level undo (up to 20 levels) for every mutation
- Language-agnostic — same workflow for Java, TypeScript, C, C++
)MD"_s
