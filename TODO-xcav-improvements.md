# xcav improvements — from agent usage analysis

> **Created**: 2026-06-03
> **Source**: Analysis of 408 xcav usage events (136 real, 272 test) + detailed agent self-assessment
>   after two Java refactors (SmartFlow base-class extraction + TNT method extraction) in a large
>   monorepo. Usage log at `~/.xcav/usage/events.jsonl` (schema_version=1, now archived/cleared).

## Context summary

### What happened

An AI agent used xcav to refactor a Java monorepo across two tasks:

1. **SmartFlow refactor**: Extracted duplicated script engine lifecycle code from 3 provider classes
   into a new `AbstractSmartFlowScriptPlugin` base class. Used whole-class `xcav_replace` on all 3
   provider files, then `xcav_edit` for import cleanup. Log seq 346–370.

2. **TNT refactor**: Extracted duplicated ID/index lookup logic from 6 methods in a 1,200-line file
   (`TNTEventFieldManagerImpl.java`) into 2 helper methods. Used method-level `xcav_replace` ×6,
   with heavy `xcav_read` beforehand. Log seq 383–408.

### What worked

- Zero xcav-caused failures in 63 Java operations.
- `xcav_blocks` for orientation in large files.
- `xcav_read` by structural block line to inspect single methods.
- `xcav_replace` for method-level replacement (TNT — the canonical workflow).
- Post-mutation block map output for immediate structural verification.
- Structural targeting eliminated wrong-block replacement risk vs plain text editing.

### What didn't work

- Agent overused whole-class `xcav_replace` in SmartFlow (admitted judgment error, not tool bug).
- `.xcav_backups/` appeared in git status — backup directory is inside the working tree.
- Post-replace output quality: indentation was ugly until Spotless ran, making review noisy.
- No structural "insert at boundary" primitive — agent anchored `xcav_edit` on neighbor method
  signatures, which is fragile.
- Agent chained mutations without explicit re-survey (relied on post-mutation block map instead).
  The analyze-xcav-usage skill flags this as an anti-pattern but doesn't distinguish implicit
  survey (mutation output includes block map) from blind chaining.

### What was considered and rejected

| Suggestion | Rejection reason |
|-----------|-----------------|
| Import hints (unused/missing) | Compiler/linter territory. xcav has no classpath, no type resolution. Even simple-name matching has false positives from `@Inject`, reflection, SPI. |
| Dry-run unified diff | `git diff` already does this. xcav's block map IS the structural diff. |
| `xcav_blocks --directory` / multi-file glob | Manual batching works fine. Adds CLI surface for marginal gain. |
| Warnings on large replacements | Agent knew they were replacing whole classes. A warning would not have stopped them. The line-count summary (task 4 below) gives the same signal without judgment. |

---

## Task 1: Move `.xcav_backups/` outside the repo

**Type**: Bug fix / UX
**Effort**: Small
**Files**: xcav backup logic (the code that creates/manages `.xcav_backups/`)

### Problem

After xcav mutations, `.xcav_backups/` is created in the current working directory. For repo-based
workflows, this shows up in `git status` as untracked noise. The agent had to manually remove it.

### Solution

Store backups in `~/.xcav/backups/<repo-path-hash>/` by default. The `<repo-path-hash>` is a hash
of the absolute path to the git repo root (or CWD if not in a repo). This keeps backups organized
per-project without leaking into the working tree.

### Considerations

- Backups are still accessible for `xcav_undo` — the undo path just needs to point to the new
  location.
- Existing `.xcav_backups/` directories in repos: ignore them (don't auto-migrate, don't break
  undo for in-flight sessions).
- No new CLI flags or config surface. The change is internal.

---

## Task 2: Post-replace formatter reminder message

**Type**: Output improvement
**Effort**: Trivial (one message to stdout after class-level replacements)
**Files**: xcav replace-block output code

### Problem

After `xcav_replace` on an entire class, the auto-re-indented code is structurally valid but may
look poorly formatted relative to the project's style (wrong indent width, brace placement, blank
line conventions). The agent couldn't tell if the output was broken or just not-yet-formatted, and
had to run Spotless before reviewing. This is especially noticeable on class-level replacements
where many lines change at once.

### Solution

After a class-level replacement, emit:

```
Class replacement complete. Run your formatter before reviewing indentation-sensitive diffs.
```

Or something equally short. Only emit for block types `class_declaration`, `class`, `interface`,
`struct` — not for individual methods where the indentation is usually fine.

### Considerations

- Don't spam this on every mutation. Class-level only.
- The message is informational, not a warning — the replacement succeeded.
- The canonical workflow after xcav is: formatter → compile → tests. This just reminds the agent
  of step 1.

---

## Task 3: Insert-before/after-block command

**Type**: New capability
**Effort**: Medium
**Files**: New command in xcav binary, updates to pi extension's tool definitions

### Problem

There is no clean way to say "insert this code before method X." The agent's workaround in TNT
was to use `xcav_edit` with `oldText` that included the next method's signature as anchor text:

```java
// oldText had to include the full header of getIndexForIds to anchor the edit
```

This is fragile: if the method signature changes, the anchor breaks. It's also semantically wrong
— the agent wants to insert *near* a method, not *replace* text that happens to be adjacent.

The existing tools don't cover this:
- `xcav_edit` requires matching anchor text somewhere in the file.
- `xcav_move` requires an existing block to relocate.
- `xcav_replace` replaces an entire block.
- `xcav_move_into` moves a block across files.

None of them say "put new code at this structural boundary."

### Solution

New subcommand `xcav insert` with two modes:

```
xcav insert --before <line> [--content "<code>"]
xcav insert --after <line> [--content "<code>"]
```

- `<line>` is any line inside a block (same targeting as `xcav_read`, `xcav_replace`).
- The inserted code goes immediately before or after that block.
- Re-indentation matches the destination.
- Content comes from `--content` or stdin.
- The destination must be a block boundary (never inside a function body).

Example usage from the TNT refactor:

```
# Insert helper method before getIndexForIds (line N)
xcav insert --before 820 --content "
  private Map<Long, TNTEventIndexDBO> getIndexFor(
      Collection<Long> ids,
      Predicate<TNTEventIndexDBO> filter) {
    // ... implementation
  }
"
```

### Considerations

- Reuses existing block-boundary computation. No new parsing.
- The destination must be at a block boundary — inserting mid-function is rejected with a clear
  error.
- Could also support `xcav edit`-style content from a file: `--file helper.java`.
- The pi extension needs a new tool definition: `xcav_insert`.
- Add to `xcav onboard` output and regenerate `skills/xcav/SKILL.md`.

---

## Task 4: Line-count summary on large replacements

**Type**: Output improvement
**Effort**: Trivial (one extra output line)
**Files**: xcav replace-block output code

### Problem

When the agent did whole-class `xcav_replace` in SmartFlow, they knew what they were replacing but
didn't get a quantitative sense of scope. A line/method count would have made the size of the
change explicit. The agent said this wouldn't have stopped the first replacement, but might have
made them reconsider the approach for the other two.

### Solution

After every `xcav_replace`, add one line to the output (before the block map):

```
Replacing class_declaration SmartFlowScriptBuilderProvider (8 methods, 95 lines)
```

Or for a method:

```
Replacing method_declaration getIndexForIds (1 method, 42 lines)
```

### Considerations

- Always print, not just for "large" replacements. The number itself communicates scale.
- Method count comes from counting nested `method_declaration` blocks inside the replaced block.
- Line count is `end_line - start_line + 1`.
- This is purely informational — no threshold, no warning logic, no judgment.

---

## Task 5: Fix analyze-xcav-usage skill — treat post-mutation block map as implicit survey

**Type**: Documentation fix
**Effort**: Small
**Files**: `/home/izashchelkin/nyla/.pi/skills/analyze-xcav-usage/SKILL.md`

### Problem

The skill's "check survey-before-mutation" rule says: for each mutation, is `ctx.prev_cmd` in
`{blocks, read}`? The agent's TNT workflow shows `prev_cmd=replace-block` for 5 of 6 mutations,
which the rule flags as "agent acted without understanding the file structure."

But `xcav_replace` prints the full updated block map on stdout after every mutation. The agent
was reading that output and using it to target the next method. This is structurally equivalent
to an explicit `xcav_blocks` call — the agent *did* have current line numbers.

### Solution

Add a third survey state to the analysis rules:

| State | prev_cmd | Meaning |
|-------|----------|---------|
| **explicit survey** | `blocks` or `read` | Agent ran a dedicated survey command. |
| **implicit survey** | `replace-block`, `replace`, `move`, `move-into`, `delete`, `edit` | Mutation that emits a block map on stdout. Acceptable if the agent inspected the output. Logs can't distinguish "output inspected" from "output ignored," so flag as a soft note, not a hard anti-pattern. |
| **no survey** | `copy`, `undo`, `help`, or first command in session | No structural context available. Flag as hard anti-pattern. |

Update the skill's section 3 ("Check survey-before-mutation pattern") with this three-state model.
Add a note that implicit survey via mutation output is acceptable but less auditable than explicit
survey.

### Considerations

- The skill doc lives at `.pi/skills/analyze-xcav-usage/SKILL.md`.
- This is a skill doc change only — no code changes to xcav or the extension.
- The distinction between "agent read the output" and "agent ignored the output" is inherently
  unknowable from JSONL alone. The skill should acknowledge this limitation.

---

## Task 6: Guidance — when to use xcav vs plain edit

**Type**: Documentation fix
**Effort**: Small
**Files**: `skills/xcav/SKILL.md` (the main xcav skill)

### Problem

The agent observed that xcav's overhead (parse → survey → target → mutate → block map) is
wasteful for single-line changes like import edits. But the skill currently says to use xcav
tools for all C/C++/Java/TS/JS editing. The agent wanted permission to use plain `edit` for
trivial changes without feeling like they're breaking discipline.

### Solution

Add a "When to use what" section to `skills/xcav/SKILL.md`:

| Change size | Use | Reason |
|------------|-----|--------|
| Single line (imports, field rename, one-line fix) | Plain `edit` | xcav overhead not justified. Text is unique and small. |
| Few lines inside one method | `xcav_edit` or plain `edit` | Either works. xcav_edit if the text isn't globally unique. |
| Entire method | `xcav_replace` | Structural safety, block map verification. |
| Entire class/struct | `xcav_replace` | Same, but be deliberate — method-level edits may be cleaner. |
| Moving code between files | `xcav_move_into` | Correct tool for the job. |
| Deleting a block | `xcav_delete` | Cleaner than manual deletion. |
| Anything where you're not sure about scope | `xcav_blocks` first | Always survey before large edits. |

Add a note: after using plain `edit`, run `xcav_blocks` to verify structure if the edit was near
block boundaries.

### Considerations

- Update `skills/xcav/SKILL.md` — the main xcav skill, not analyze-xcav-usage.
- Keep it a short table, not a treatise.
- The xcav extension's tool descriptions don't need to change.

---

## Execution order

1. **Task 2 + Task 4** (trivial, same output code path, can do together)
2. **Task 1** (backup location — small code change, test that undo still works)
3. **Task 3** (insert command — medium, needs test fixtures and pi tool definition)
4. **Task 5 + Task 6** (doc changes — update both skills, regenerate SKILL.md from `xcav onboard`)
5. **Verify**: run `./scripts/install_xcav.sh && XC_BIN=/usr/local/bin/xcav ./xcav/tests/run_tests.sh`
6. **Verify**: re-run the analyze-xcav-usage skill against fresh usage logs to confirm the implicit-survey rule catches real patterns correctly
