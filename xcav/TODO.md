# xcav triaged TODOs from AI edit-companion field notes

Context: xcav is being used as the structural-edit layer for an AI agent in a
large Java and TypeScript monorepo. The target loop is:

1. Inspect/read structurally.
2. Apply a reversible tree-sitter-validated mutation.
3. Normalize only what was touched.
4. Continue editing without risking unstaged user work.
5. Run the heavyweight compiler/test pass once at the end.

## Source-level philosophy

Snapshot date: 2026-06-02.

xcav is not an IDE, formatter, compiler, LSP, or full semantic refactoring
engine. The source points to a smaller and more useful identity: a fast,
local, reversible, syntax-aware structural edit CLI for agents.

The code is intentionally simple:

- `language.cc`: extension-based language detection, no project model.
- `tree_sitter_util.cc`: thin tree-sitter wrappers, no persistent parser state.
- `block_query.cc`: structural block discovery plus compact, un-indented output
  for token-efficient agent reads.
- `edit_ops.cc`: mutation commands that operate on bytes and tree-sitter block
  ranges.
- `backup.cc`: per-file undo through local backups.
- `onboard.inl`: agent usage is thin wrappers around the CLI; all real logic
  stays in the C++ binary.

The desired trust envelope is:

1. Build a candidate output in memory.
2. Validate with tree-sitter, distinguishing new errors from pre-existing
   errors when possible.
3. Preview via dry-run and structural diff.
4. Commit only after validation.
5. Save an undo entry that cannot accidentally enter commits.

Scope guardrails:

- Prefer syntax-aware, structural guarantees over semantic refactoring.
- Prefer conservative refusal/warnings over trying to infer a whole project.
- Keep external agent integrations as thin wrappers, skills, commands, or MCP
  adapters around the CLI.
- Do not make `tidy` or any whole-file formatter part of the inner mutation
  loop.
- Deeper semantic checks belong in optional LSP/compiler integrations, not in
  the core block mover.

## Regression guardrails

Do not regress these behaviors:

- `edit --stdin` lax whitespace matching, including flat-indented oldText
  matching tab-indented source.
- `edit` Unicode normalization, including normalized punctuation matching source
  punctuation variants.
- `edit` tree-sitter validation that distinguishes new errors from pre-existing
  errors and leaves files untouched on rejection.
- `delete` cleanup of orphaned leading comments and extra blank lines.
- Byte-identical, instant `undo`.
- Post-mutation structural block diff output.

## Triage summary

| Priority | Area | Why it matters | First outcome |
|---|---|---|---|
| P0 | Regression tests | Locks down the reported failures before changing code. | Repro fixtures for indentation, dry-run, backup, and match diagnostics. |
| P0 | Indentation for structural mutations | Decides whether agents can trust `move`, `move-into`, and `extract`. | Moved blocks preserve tab/space style and destination nesting. |
| P0 | Dry-run for mutators | Lets agents preview risky structural changes before writing. | `move`, `move-into`, `extract`, and `delete` support no-write previews. |
| P0 | Backup commit safety | Prevents accidental `git add -A` of undo artifacts. | Backups are ignored or moved outside the repo. |
| P0 | C++ CLI/path reliability | C++ is the home codebase; basic commands cannot assert on common relative paths. | `xcav blocks xcav/edit_ops.cc` and similar paths are safe. |
| P0 | C++ move/extract polish | Repo-style section comments and namespace formatting should move cleanly with blocks. | C++ block moves/extracts preserve nearby section/doc context. |
| P0 | Match diagnostics | Avoids wasting agent turns on ambiguous edits. | `edit` distinguishes absent vs ambiguous matches. |
| P1 | Java/TS extract validity | Prevents silent generation of uncompilable files. | Method-level unsafe extracts fail clearly before any write. |
| P1 | `read --name` and `inline` diagnostics | Removes silent overload/call-form ambiguity. | Multiple matches and unsupported inline cases are explicit. |
| P1 | JSON output | Makes wrappers and MCP adapters robust once mutation result objects exist. | Stable structured output mode for success, failure, warnings, and diffs. |
| P1 | Direct agent integrations | Makes xcav installable as an agent-native editing tool instead of only documented CLI advice. | Codex and Claude Code plugin packs expose the same safe structural-edit workflow. |
| P2 | Conservative semantic warnings | Converts obvious semantic breakage into reviewable notes without becoming an LSP. | Moved blocks report syntactically obvious unavailable symbols. |
| P2 | Cross-file atomicity/session undo | Matches operation-level agent behavior. | Multi-file operations rollback/undo as one step. |
| P2 | Parse cache/daemon | Keeps repeated operations fast at monorepo scale. | Optional persistent parsing path. |

## Source-code triage

Snapshot date: 2026-06-02.

| Area | Current source state | Triage impact |
|---|---|---|
| Backups | `xcav/backup.cc` uses fixed `kBackupDir = ".xcav_backups"` and `SaveBackup()` only creates directories. | P0.6 is a small, direct change: create `.xcav_backups/.gitignore` or move the root. |
| CLI path handling | `CmdBlocks()` null-terminates `pathBuf`, but single-file mode calls `ListBlocks(filePath, alloc)` with the raw token. Probe: `xcav blocks xcav/edit_ops.cc` asserted in `Span::CStr`, while `./xcav/edit_ops.cc` worked. | P0.17 should fix this before broader polish work. |
| Structural mutation indentation | `MoveBlock()`, `MoveBlockInto()`, and `BlockExtract()` in `xcav/edit_ops.cc` compute indentation as byte counts and emit literal spaces. `BlockExtract()` hard-codes namespace indent to 4 spaces. | P0.8-P0.12 need a shared indent style detector/emitter before fixing each command. |
| C++ section comments | `MoveBlock()` moves only the tree-sitter node range. Probe: moving `StrEq` in `text_util.cc` left `// ─── StrEq ───` behind and inserted the function without its section header. `BlockExtract()` removed the function but left the same section separator in source. | P0.18 should define comment/section ownership for C++ moves/extracts. |
| C++ extract output | Probe: extracting `StrEq` to `str_eq.h` generated `#pragma once`, included `xcav/text_util.h`, indented the function under `namespace nyla`, and emitted a non-`inline` function definition in a header. | P0.19/P1.1 should make C++ extract output match repo style and avoid dangerous header definitions. |
| Tab conversion | `EditSafe()` whitespace cleanup and `TidyFile()` convert tabs to four spaces. | Scoped formatting must not reuse whole-file `tidy` behavior for tab-preserving Java/TS workflows. |
| Mutation validation | `MoveBlock()`, `MoveBlockInto()`, `DeleteBlock()`, and `BlockExtract()` build candidates and write them without the tree-sitter new-error gate used by `EditSafe()`. | P0.14 should centralize candidate validation before dry-run/write support. |
| Dry-run parsing | `CmdMove()`, `CmdMoveInto()`, `CmdDelete()`, and `CmdExtract()` in `xcav/main.cc` do not parse `--dry-run`; operation functions return only `bool`. | P0.14-P0.15 need an internal mutation result/plan type, not just flag parsing. |
| Cross-file atomicity | `MoveBlockInto()` saves both backups, writes destination, then writes source. `BlockExtract()` writes the new file before backing up/writing the source. | P2.1 is real correctness work; source currently has partial-write failure windows. |
| New-file extract safety | `BlockExtract()` copies imports, emits C/C++ wrapper bits, and treats Java/TS/JS as plain import-copy module output. | P1.1 should first refuse unsafe Java/TS member extracts before adding wrapper generation. |
| Edit match diagnostics | `EditSafe()` already distinguishes `matchCount == 0` from `matchCount > 1`, but ambiguous output lacks line numbers and there is no `--occurrence`/`--all`. | P0.15 is refine/extend, not start-from-zero. |
| Structural diff signal | Probe: a one-line edit in `StrEq` reported later `IncludePath` and `NormalizeText` blocks as removed/added because line ranges shifted. | P0.20 should match blocks by stable identity, not only line ranges. |
| `read --name` | `CmdRead()` scans blocks and returns on the first suffix/exact match. | P1.5 can be localized to collecting all matches before printing. |
| `inline` | `InlineFunctionCall()` only accepts simple identifier call targets and searches the current tree for the callee. | P1.6-P1.7 should split diagnostics for qualified calls, cross-file calls, and unsupported bodies. |
| JSON output | Commands print via `LOG()`/`FileWriteFmt()` and do not share a result object. | P2.1-P2.2 should come after mutation result types exist. |
| Existing agent docs | `xcav/onboard.inl` documents Pi thin wrapper tools and a Claude Code "custom tools" section, but there are no checked-in Codex or Claude plugin directories. | P1 direct integration work should package the same thin-wrapper philosophy as installable plugin packs. |
| Tests | `xcav/tests/run_tests.sh` has helpers for blocks/move/delete/edit/extract/undo and currently deletes `.xcav_backups` relative to cwd. | P0 regression work can extend the existing shell harness before adding new frameworks. |

## Agent integration surfaces checked

- Codex supports plugins and skills. OpenAI's plugin examples use
  `.codex-plugin/plugin.json` with optional `skills/`, `.mcp.json`,
  `.app.json`, plugin-level `agents/`, `commands/`, `hooks.json`, and assets:
  https://github.com/openai/plugins
- OpenAI describes Codex plugins as connections to tools/data and skills as
  reusable playbooks/processes:
  https://openai.com/academy/codex-plugins-and-skills/
- Claude Code supports plugin marketplaces under `.claude-plugin/marketplace.json`.
  Plugins can include skills, agents, hooks, MCP servers, and LSP servers:
  https://code.claude.com/docs/en/plugin-marketplaces
- Claude Code install flow supports `/plugin marketplace add` and
  `/plugin install <plugin>@<marketplace>`:
  https://code.claude.com/docs/en/discover-plugins

## Suggested implementation order

1. Add regression tests that reproduce the field report.
2. Fix backup commit safety as the smallest immediate safety win.
3. Fix C++ CLI path handling for `blocks` and add C++ probe regressions.
4. Add a shared mutation plan/result path with validation before writes.
5. Wire `--dry-run` and structural diff for the structural mutators.
6. Add shared indentation detection/emission, then fix `move`, `move-into`, and
   `extract`.
7. Fix C++ section-comment ownership and extract formatting/header behavior.
8. Fix `edit` ambiguous-vs-absent diagnostics.
9. Refuse unsafe Java/TypeScript extracts before any write.
10. Add `read --name` overload behavior and clearer `inline` errors.
11. Add JSON output once mutation result objects exist.
12. Add installable Codex and Claude Code plugin packs that wrap the safer P0/P1
   workflow.
13. Add conservative semantic/import warnings without requiring a project model.
14. Add cross-file operation undo/atomicity.
15. Add parse cache or daemon mode.

## P0: Regression harness

### P0.1 Add fixtures for tab-indented Java and TypeScript

- Add a Java fixture with a tab-indented class and at least three methods.
- Add a TypeScript fixture with a tab-indented class or exported object and
  nested methods/functions.
- Include destination positions inside the class/object and near the closing
  brace.
- Keep expected output byte-exact so tab preservation is tested.

### P0.2 Add structural mutation indentation tests

- Test `move` within a tab-indented Java class.
- Test `move` within a tab-indented TypeScript file.
- Test `move-into` from one tab-indented file into another.
- Test that closing braces remain separated and do not collapse into `}}`.
- Test that unrelated lines are byte-identical after mutation.

### P0.3 Add dry-run no-write tests

- Test `move --dry-run` leaves the file unchanged.
- Test `move-into --dry-run` leaves both files unchanged.
- Test `extract --dry-run` does not create the destination file.
- Test `delete --dry-run` leaves the file unchanged.
- Test dry-run creates no backup entry.
- Treat these four as the first structural-mutator scope. Later decide whether
  `replace`, `replace-block`, `inline`, and `tidy` need dry-run too.

### P0.4 Add backup safety tests

- Test backup creation does not leave committable untracked content, or creates a
  `.xcav_backups/.gitignore` that ignores backup payloads.
- Test multi-level `undo` still works after the backup path/ignore change.

### P0.5 Add match diagnostic tests

- Test `edit` with 0 matches reports an absent match.
- Test `edit` with exactly 1 match reports success.
- Test `edit` with multiple matches reports ambiguity, count, and line numbers.

### P0.6 Add C++ home-repo polish probes

- Add probe fixtures copied from this repo's C++ style, especially namespace
  blocks and `// ─── Section ───` separators.
- Test `xcav blocks xcav/edit_ops.cc` and `xcav blocks ./xcav/edit_ops.cc`.
- Test moving a function with a preceding section separator.
- Test extracting a function from inside `namespace nyla`.
- Test that a one-line edit produces a useful structural diff without false
  remove/add noise for every following block.
- Keep these probes in the shell harness before building new test frameworks.

## P0: Trust blockers

### P0.7 Backup commit safety

Smallest acceptable fix:

- When creating `.xcav_backups/`, also create `.xcav_backups/.gitignore`.
- Ignore all backup payloads while allowing the `.gitignore` itself to exist.
- Update `xcav/tests/run_tests.sh`, which currently removes `.xcav_backups`
  relative to cwd, to assert the ignore behavior.
- Update `undo` docs to mention the ignore file.

Better follow-up:

- Move backups to a per-repo/per-path location under the system cache or temp
  directory.
- Preserve lookup stability across invocations.
- Keep compatibility with existing `.xcav_backups/` long enough for old undo
  entries to be recoverable.

### P0.8 Shared indentation style detector

- Add a helper, likely in `text_util.cc`, that detects the file's dominant indent
  unit.
- Prefer indentation from the destination enclosing block when available.
- Distinguish tabs, 2-space, 4-space, and mixed indentation.
- Preserve existing tab indentation; do not normalize tabs to spaces in mutation
  paths.
- Define fallbacks for files with too little indentation evidence.
- Add unit-level tests for detection from representative snippets.

### P0.9 Shared indentation emitter

- Add a helper that emits one destination indent prefix from detected style and
  nesting depth instead of writing `newIndent` literal spaces.
- Preserve relative indentation inside the moved block.
- Preserve blank lines without adding stray spaces.
- Preserve trailing newline behavior consistently.
- Reuse this helper in `move`, `move-into`, `extract`, and any scoped formatter.

### P0.10 Fix `move` indentation

- Compute source block baseline indentation from non-blank lines.
- Compute destination nesting depth from the destination context.
- Re-indent the moved block through the shared emitter.
- Preserve spacing around the insertion point.
- Replace the current `MoveBlock()` byte-count indent loop with the shared
  helper.
- Run existing tree-sitter validation after constructing the candidate output.
- Confirm the structural block diff still reports the move clearly.

### P0.11 Fix `move-into` indentation

- Use destination file style, not source file style, for inserted block
  indentation.
- Replace the current `MoveBlockInto()` literal-space reindent loop.
- Keep `--copy-includes` behavior unchanged except for formatting cleanup.
- Validate both source and destination candidate files before writing either.
- Defer writes until both candidate files are built and validated.

### P0.12 Fix `extract` indentation

- Re-indent extracted output using the new file's top-level/package/module style.
- Replace `BlockExtract()`'s hard-coded 4-space namespace/member indentation.
- Preserve valid namespace/package/module wrapping behavior per language.
- Ensure source-file removal does not collapse adjacent braces or comments.
- Validate source and destination candidates before writing.

### P0.13 Scoped formatting option

- Decide whether the normal mutation path is already enough once P0.8-P0.12 are
  fixed.
- If not, add `--format` that only re-indents the touched block/range.
- Do not call whole-file `tidy` from the inner mutation loop.
- Add tests proving untouched ranges remain byte-identical.

### P0.14 Dry-run infrastructure

- Introduce an internal mutation plan/result shape containing touched files,
  candidate bytes, structural diff data, validation status, and warnings.
- Convert `MoveBlock()`, `MoveBlockInto()`, `DeleteBlock()`, and
  `BlockExtract()` from immediate-write operations into plan-build plus commit
  phases.
- Keep this as the shared trust path for structural mutators rather than a
  general formatter or semantic engine.
- Make write/backup creation a final commit step after validation.
- Let dry-run stop before the commit step.
- Share this path across `move`, `move-into`, `extract`, and `delete`.

### P0.15 Wire `--dry-run` and `--diff` behavior

- Parse `--dry-run` for `move`, `move-into`, `extract`, and `delete`.
- Print the block match and structural diff.
- For cross-file operations, print per-file candidate summaries.
- Add `--diff` if needed to mirror `edit --dry-run --diff`.
- Ensure dry-run never writes files, creates backups, or creates extracted files.
- After this lands, decide whether dry-run should extend to `replace`,
  `replace-block`, `inline`, and `tidy` as a separate compatibility pass.

### P0.16 Improve `edit` match diagnostics

- Keep the current `matchCount == 0` vs `matchCount > 1` distinction.
- Count exact, normalized, and lax whitespace matches separately if useful.
- Report 0 matches as absent.
- Report more than 1 match as ambiguous.
- Include candidate line numbers for ambiguous matches.
- Keep successful single-match behavior stable.

### P0.17 Add controlled multi-site edit options

- Add `--occurrence N` for choosing one match when ambiguity is intentional.
- Add `--all` for replacing every match.
- Make both options explicit errors when combined.
- Include match count in dry-run output.

### P0.18 Fix `blocks` single-file path handling

- Use the null-terminated `safePath`/`pathBuf` in `CmdBlocks()` single-file mode.
- Add a regression for `xcav blocks xcav/edit_ops.cc` from repo root.
- Check other commands for the same raw-token path pattern.

### P0.19 Carry C++ section/doc comments with moved blocks

- Define ownership rules for adjacent `// ─── Section ───` separators and normal
  doc comments.
- For `move`, include the owned leading separator/doc comments in the moved
  byte range.
- For `extract`, remove the owned separator/doc comments from source or move them
  into the destination when appropriate.
- Preserve file-level header comments.

### P0.20 Make structural diff stable under line shifts

- Match before/after blocks by structural path, type, and content/name where
  possible instead of line range alone.
- Report a one-line function edit as a change to that function, not as
  remove/add noise for every later block.
- Keep the diff compact enough for agent consumption.

### P0.21 Make C++ extract output match repo style

- Detect namespace indentation style; in Nyla-style files, do not indent
  namespace contents by default.
- If extracting to a header, avoid emitting non-`inline` function definitions
  unless explicitly requested.
- Prefer extracting free function implementations to `.cc` files or require a
  clear output-mode decision.
- Keep generated includes compatible with existing `"nyla/..."` style and avoid
  redundant source includes where possible.

## P1: Correctness and ergonomics

### P1.1 Refuse unsafe Java/TypeScript method-level `extract`

- Detect when the selected block is a method/member that cannot be valid at file
  top level.
- Fail before modifying the source file.
- Explain the enclosing type and why extraction is unsafe.
- Keep top-level class/interface/type/module extracts working.

### P1.2 Consider valid Java/TypeScript extract wrappers only after refusal works

- Preserve Java `package` declarations.
- Generate or preserve the correct top-level class/interface wrapper when the
  requested extract can be made valid.
- For TypeScript, preserve module/export shape when practical.
- Avoid generating wrapper code until the naming and import behavior is
  deterministic.
- Do not let wrapper generation delay the safer first step: refuse invalid
  member-level extracts before source modification.

### P1.3 Keep import handling conservative

- Collect identifiers referenced by the moved/extracted block.
- Keep imports that provide referenced identifiers.
- Preserve side-effect imports.
- Leave conservative imports in place when the dependency cannot be proven.
- Report any imports that were copied conservatively.
- Avoid turning import pruning into a project-wide resolver. If correctness is
  uncertain, copy/import conservatively and warn.

### P1.4 Warn conservatively about unavailable symbols after moves

- Collect identifiers referenced by the moved block.
- Collect obvious symbols available in the destination scope.
- Warn for references that appear to come from the source scope but are absent at
  destination.
- Include symbol names and the destination file/line context.
- Do not block the move unless syntax validation fails.
- Keep this syntactic and local. Rich symbol resolution belongs in optional
  LSP/compiler integrations.

### P1.5 Make `read --name` overload-aware

- Find all suffix/name matches instead of stopping at the first match.
- If multiple matches exist, list line ranges and structural names.
- Add `--all-matches` or make multiple output the default.
- Preserve current single-match output for unambiguous names.

### P1.6 Improve `inline` call diagnostics

- Report `no call expression at line` only when there is truly no call.
- Report `unsupported qualified call` for unsupported receivers.
- Report `unsupported cross-file call` when the callee is unavailable locally.
- Report `unsupported body shape` for multi-statement or otherwise unsupported
  bodies.
- Report `callee not found` distinctly.

### P1.7 Support simple qualified same-class inline calls

- Support `this.foo(...)` when `foo` resolves to a same-class method.
- Support class-local receiver forms that are unambiguous.
- Keep cross-file and dynamic dispatch cases as explicit unsupported errors.

## P1: Direct agent integrations

### P1.8 Define a shared xcav agent contract

- Specify canonical tool names: `xcav_blocks`, `xcav_read`, `xcav_edit`,
  `xcav_replace`, `xcav_move`, `xcav_move_into`, `xcav_delete`,
  `xcav_extract`, `xcav_inline`, `xcav_tidy`, and `xcav_undo`.
- Specify argument schemas for each command, including `--dry-run`, `--diff`,
  `--json`, and `--copy-includes` once available.
- Specify the expected workflow: inspect with `blocks`/`read`, preview mutators
  with `--dry-run`, apply, inspect structural diff, undo on failure.
- Make the contract agent-neutral so Codex and Claude Code wrappers do not drift.
- Include safety defaults: dry-run first for `move`, `move-into`, `extract`, and
  `delete`; prefer `edit --stdin`; never run whole-file `tidy` implicitly.

### P1.9 Create a Codex plugin pack

- Add a Codex plugin directory with `.codex-plugin/plugin.json`.
- Include a Codex skill that mirrors the repo-local xcav workflow and tells Codex
  when to use each command.
- Include wrapper scripts or MCP configuration that expose the canonical xcav
  tools directly to Codex instead of relying on ad hoc shell commands.
- Include install/update instructions for a personal or repo/team marketplace.
- Add validation that the plugin manifest, skill paths, and wrapper scripts are
  loadable from an installed plugin cache location.
- Keep the plugin compatible with the local CLI binary path and a configurable
  `XCAV_BIN` override.

### P1.10 Create a Claude Code plugin pack

- Add a Claude Code plugin directory with `.claude-plugin/plugin.json`.
- Add a `.claude-plugin/marketplace.json` entry or marketplace scaffold for local
  and team installation.
- Replace the current `xcav/onboard.inl` custom-tool prose with installable
  plugin assets where practical.
- Expose slash commands or command wrappers for the canonical xcav tools.
- Add a Claude Code skill/playbook for the structural-edit workflow.
- Consider a non-mutating hook that reminds Claude to use xcav for supported
  C/C++/Java/JS/TS structural edits, without auto-running mutators.
- Keep plugin paths self-contained because Claude Code copies installed plugins
  into a cache location.

### P1.11 Add an optional MCP server for both agents

- Build a small MCP server or command adapter that exposes xcav subcommands as
  typed tools.
- Use `--json` output once P1.14 exists; until then, treat human output as a
  temporary compatibility mode.
- Make the server stateless by default and pass cwd/project root explicitly.
- Return structured errors for absent files, unsupported languages, ambiguous
  matches, syntax validation failures, and dry-run previews.
- Package the MCP config in both the Codex and Claude Code plugin packs.
- Keep all mutation logic in the xcav binary. The MCP server should adapt I/O,
  not reimplement block discovery, indentation, or validation.

### P1.12 Add agent integration tests

- Add smoke tests that validate generated Codex plugin manifests and paths.
- Add smoke tests that validate generated Claude Code plugin manifests and
  marketplace entries.
- Add wrapper tests that call each canonical tool against xcav fixtures.
- Add a dry-run-first workflow test for one Java or TypeScript move.
- Add docs tests or fixture checks so `xcav onboard` does not advertise stale
  non-plugin setup as the preferred integration path.

## P1: Structured output

### P1.13 Define a JSON schema

- Add a schema covering command status, files touched, match byte offsets, match
  line numbers, changed blocks, warnings, new syntax errors, pre-existing syntax
  errors, and unified diff text.
- Base the schema on the mutation plan/result shape from P0.14.
- Keep field names stable.
- Include an explicit schema version.

### P1.14 Add `--json` output mode

- Support `--json` for every command.
- Keep human output as the default.
- Ensure errors are structured in JSON mode.
- Add golden-output tests for representative success and failure cases.

## P2: Agent integration and scale

### P2.1 Cross-file atomicity

- Build all candidate file outputs before writing any file.
- Validate every candidate before writing.
- If a write fails after another file was written, restore the written file from
  the operation backup.
- Add failure-injection tests if practical.

### P2.2 Session-level undo

- Record each operation as a single undo entry with all touched files.
- Add `xcav undo --last` or equivalent operation-level undo.
- Keep existing `xcav undo <file>` behavior.
- Document how per-file and operation-level undo interact.

### P2.3 Persistent parse cache or daemon mode

- Add an optional mode that keeps parsed trees for recently touched files.
- Invalidate on mtime/size/content changes.
- Keep one-shot CLI behavior unchanged.
- Measure benefit on large Java/TypeScript files before expanding complexity.

## Open design decisions

- Backup strategy: `.xcav_backups/.gitignore` is quick and compatible; external
  cache storage is cleaner but needs migration and lookup design.
- Java/TypeScript extract: refusing unsafe member extracts is the safest first
  step; generating wrappers is useful but should come after deterministic tests.
- Scoped formatting: decide after shared indentation fixes whether a separate
  `--format` flag is still needed.
- JSON output: add after mutation result objects stabilize, otherwise wrappers
  will depend on transitional fields.
