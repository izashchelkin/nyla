# xcav — Structural Code Mover for Agents

xcav is a CLI tool for structural code operations — listing, reading, moving,
deleting, and editing code blocks. It uses tree-sitter to parse C, C++, Java,
JavaScript, and TypeScript, understanding block boundaries even through syntax
errors.

Build: `cmake --build build/linux-debug --target xcav`
Install: `./scripts/install_xcav.sh`
Test: `XC_BIN=/usr/local/bin/xcav ./xcav/tests/run_tests.sh` (136 tests, 5 phases)

## ⚠️ Pi extension MUST match CLI binary

The Pi extension (`.pi/extensions/xcav/index.ts`) registers tools that wrap the
`xcav` binary. Every tool in the extension MUST have a corresponding command in
the binary, and vice versa. When adding or removing a CLI command, update the
extension in the same commit.

Verify: `grep 'name: "xcav_' .pi/extensions/xcav/index.ts` vs `xcav help` output.

## Design principles

- **`read` output = `edit` input.** Default is un-indented; `--raw` for exact.
  Edit handles un-indented matching automatically.
- **Prefer failure over wrong answer.** Ambiguity or no-match → fail with
  diagnostics, don't guess.
- **`undo` makes mistakes cheap.** The agent should feel safe experimenting.
- **Remove before adding.** Commands not in the core loop increase surface area.
  Tier 4 tools (`move_into`, `copy`) are on notice.
- **xcav is structural, not semantic.** Tree-sitter for block detection, not
  validation. No type checking, no import pruning, no formatting.
- **Tree-sitter boundaries are an implementation detail.** `read` rounds to
  whole lines — the agent never sees partial-line truncation.
- **`xcav edit` is line-based matching.** Matches lines by content (ignoring
  indentation), copies indentation from matched lines to replacement. Unicode
  normalization handles LLM-produced smart quotes/em-dashes. No tree-sitter
  validation — `xcav edit` works on any text file.
- **Java signature display.** `blocks` shows return type, modifiers,
  parameter types+names, and `throws` clause for Java methods. Annotations
  like `@Override` are displayed inline. Pure CST parsing — no semantic
  analysis.
- **Large-file prevention.** `read` auto-truncates files >500 lines or
  >50KB with a clear message. Use `--offset`/`--limit` to navigate.
- **Languages: C, C++, Java, JavaScript, TypeScript, TSX.** Detected by
  file extension. Tree-sitter parsers tolerate syntax errors.

## Out of scope

- Semantic analysis, type checking, import pruning
- Whole-file formatting, daemon mode, plugin packs
- LSP-style extract method, inline variable, or rename operations
- Compiler/formatter fixes — these are checkpoint work after the move, not part of the refactor loop

## When to use xcav

xcav is for **structural change**, not cleanup. Think of the workflow as:

1. **Survey** (`blocks`) — answer "what is the exact block?" and "what will change?"
2. **Read** (`read`) — confirm you've targeted the right unit
3. **Act** (`move`/`delete`/`replace`/`edit`) — apply one structural change
4. **Verify** (`blocks`) — did the target land where I expected?
5. **Repeat** — re-survey before each subsequent move; line numbers shift
6. **Checkpoint** — compile + format only after the full sequence is done

The primary validation loop is **structural** (xcav_blocks), not semantic.
Compiler, formatter, and semantic fixes are checkpoint work after the refactor
is structurally complete. Don't mix them into the move loop.

## Worked example: multi-step refactor

Starting file — a large class with helper methods that should be free functions:

```
$ xcav blocks renderer.cc
renderer.cc (5 blocks)
     1-   5 func WipeBuffer
     7-  45 func Renderer::Draw
    47-  60 func Renderer::ComputeLighting
    62-  85 func Renderer::BlitFramebuffer
    87- 110 class Renderer
```

**Step 1 — Survey.** The blocks output tells us `ComputeLighting` and
`BlitFramebuffer` are methods (inside the class at line 87). `WipeBuffer` is a
free function at the top.

**Step 2 — Confirm.** Read the candidate block to verify:

```
$ xcav read renderer.cc 47
auto ComputeLighting(vec3 const& dir) -> float {
    return dot(dir, sunDirection) * ambientOcclusion;
}
```

Good — this is the right block. It's a small helper.

**Step 3 — Move it out of the class.** Move-into a new file for lighting helpers:

```
$ xcav move-into renderer.cc 47 lighting.cc 0 --copy-includes
```

**Step 4 — Verify structurally.**

```
$ xcav blocks renderer.cc
renderer.cc (4 blocks)
     1-   5 func WipeBuffer
     7-  45 func Renderer::Draw
    62-  85 func Renderer::BlitFramebuffer
    87- 110 class Renderer

$ xcav blocks lighting.cc
lighting.cc (1 block)
     1-  14 func ComputeLighting
```

Block moved. Line numbers shifted (BlitFramebuffer was 62-85, now 62-85 still —
wait, actually it depends on indentation). The key check: no orphaned blocks,
no name collision, surrounding context is correct.

**Step 5 — Replace the next block.** `BlitFramebuffer` is too large to move as-is.
Read it, write a replacement to a temp file, then replace:

```
$ xcav read renderer.cc 62 > /tmp/old_blit.txt
# ... write replacement to /tmp/new_blit.txt ...
$ xcav replace renderer.cc 62 /tmp/old_blit.txt /tmp/new_blit.txt
```

**Step 6 — Verify again.**

```
$ xcav blocks renderer.cc
renderer.cc (4 blocks)
     1-   5 func WipeBuffer
     7-  45 func Renderer::Draw
    62-  85 func Renderer::BlitFramebuffer
    87- 110 class Renderer
```

Structure is clean. Now checkpoint with build + format.

```
$ cmake --build build/linux-debug --target renderer
$ cmake --build build/linux-debug --target format
```

The key takeaway: **every step was verified structurally before proceeding.**
No guessing about line numbers, no hoping the block boundaries were right.

## Changelog

### 2026-06-02 — Java method signatures, test coverage

- **Java method signatures in `blocks`**: return type, modifiers (`static`,
  `final`, `abstract`), parameter types+names, `throws` clause. Pure CST
  parsing — no semantic analysis (doesn't distinguish checked vs unchecked).
  Constructor signatures also shown (e.g. `public DataService(String
  configPath) throws IOException`).
- **Annotation display**: `@Override`, `@Deprecated @SuppressWarnings(...)`,
  etc. shown inline in `blocks` output. Both same-line and new-line styles
  supported. Multi-annotation works.
- **`--name` resolution**: when a class and its constructor share the same
  name, container types (class/struct/enum/interface) are preferred over
  methods/constructors.
- **Full test suite**: 136 tests across 5 phases covering Java survey/read,
  single-file mutations (move, delete, edit, replace), cross-file copy/
  move-into (with `--copy-includes`), edge cases (empty files, interface-only,
  abstract-only, enum-only), plus C cross-file and TSX backfill.
- **Pre-existing `set -e` bug fixed in test runner**: standalone `set -e`
  lines in error-path test sections were silently enabling `errexit`
  globally, causing `diff | sed` in `fail()` to kill the script. Fixed by
  commenting them out (they were never correct — `errexit` was off at
  script startup).

### 2026-06-02 — Simplified `xcav_read` + large-file prevention

`--all` and default mode no longer iterate blocks. They simply print the
entire file content. No headers, no structural parsing. Un-indented by default,
`--raw` for exact indentation, `--numbers` for line numbers.

Large files (>500 lines or >50KB) are auto-truncated to first 500 lines with:
`[Truncated to first 500 of N lines. Use --offset/--limit to read more.]`

### 2026-06-02 — `read → edit` parity achieved

- `read` no longer outputs `// name (type, lines N-M)` headers
- `ReadBlock` rounds tree-sitter byte ranges to line boundaries (no missing `;`)
- `edit` error diagnostics show what was looked for + partial match hints

### 2026-06-02 — Pi integration: show tool arguments

All 8 xcav tools now display their arguments in the output header via
`fmtArgs()` / `wrapOutput()` helpers. String params >80 chars are truncated.
