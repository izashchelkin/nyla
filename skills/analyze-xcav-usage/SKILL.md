---
name: "analyze-xcav-usage"
description: "Analyze xcav usage logs to debug agent behavior, find failure patterns, and optimize tool effectiveness. Reads ~/.xcav/usage/events.jsonl."
version: 1
created: "2026-06-03"
---
## When to Use
Use when reviewing xcav effectiveness, debugging agent behavior, investigating failure patterns, or preparing to optimize the xcav tool. The JSONL file at ~/.xcav/usage/events.jsonl contains one JSON event per xcav invocation with schema_version=1 fields: cmd, cat (survey/mutation/recovery/help), target (path/ext/lang), args (has_dst/cross_file/has_line/mode), outcome (ok/code/ms/mutated/error), ctx (prev_cmd/redundant_blocks).

## Procedure
1. Check log health: `wc -l ~/.xcav/usage/events.jsonl` and verify JSON validity.
2. Run the analysis queries below for a comprehensive report.
3. Check survey-before-mutation pattern. There are three survey states:

   | State | prev_cmd | Meaning |
   |-------|----------|---------|
   | **explicit survey** | `blocks` or `read` | Agent ran a dedicated survey command before mutating. |
   | **implicit survey** | `replace-block`, `replace`, `move`, `move-into`, `delete`, `edit`, `insert` | Mutation that emits a block map on stdout after completion. Acceptable if the agent inspected the output. Logs can't distinguish "output inspected" from "output ignored," so flag as a soft note, not a hard anti-pattern. |
   | **no survey** | `copy`, `undo`, `help`, or first command in session | No structural context available. Flag as hard anti-pattern. |

   Note: implicit survey via mutation output is structurally equivalent to an explicit `blocks` call — the agent has current line numbers either way. The distinction is inherently unknowable from JSONL alone.
4. Check read-before-edit pattern: for edit commands, is prev_cmd=read? Edit without read means the agent used oldText from memory rather than from xcav_read output.
5. Check redundant blocks: count ctx.redundant_blocks=true. High rate means agents call blocks after every mutation even when unnecessary.
6. Analyze error distribution: group by outcome.error to find top failure reasons (bad_args, block_not_found, text_not_found, operation_failed).
7. Check recovery patterns: after a failure (ok=false), does the agent retry the same command or switch to undo? Look at sequences of (mutation fail) → (undo) → (retry).
8. Check stale line numbers: block_not_found rate over time. Increasing rate means agents are using cached line numbers after file mutations without re-running blocks.
9. Check cross-file vs within-file usage: ratio of has_dst to cross_file. If agents use move when move-into would be better (or vice versa), there may be confusion.
10. Check session duration patterns: group by session_id, look at max(ms) and event count per session. Sessions with many events but short duration may indicate agent churn.
11. Check language distribution: group by target.lang. If one language dominates errors, the tree-sitter grammar may need attention.
12. Correlate ms (duration) with ok: are failures faster (immediate rejection) or slower (timeout/processing)? Fast failures suggest arg validation; slow failures suggest I/O or parsing issues.
## Pitfalls
- The JSONL file is append-only and grows unboundedly — archive periodically if analyzing old data.
- Session IDs change across reboots (boot_id changes). Cross-session analysis requires joining on target.path + ts.
- Error tags are best-effort: sub-function failures (MoveBlock, EditSafe) report as 'operation_failed' because the specific error type isn't exposed through the API. The actual error is in stderr, not the JSONL.
- help/onboard commands have no target field — filter them out when analyzing target-specific patterns.
- Undo doesn't set s_exitCode on failure (RestoreBackup returns false but CmdUndo doesn't check). Undo errors appear as ok=true in logs.
- Concurrent xcav invocations produce interleaved JSONL entries. Sort by ts before analyzing sequences.
- Seq is per-JSONL-file, not per-session. If the file is deleted, seq resets to 1.

## Verification
1. Run `wc -l ~/.xcav/usage/events.jsonl` — should be non-zero if xcav has been used.
2. Run the failure analysis query — should produce a table of commands and failure counts.
3. Spot-check a few failure events: look at the sequence of prev_cmd leading up to the failure to understand the agent's workflow.
4. Check that error distribution isn't dominated by one error type (if it is, that's the top priority to fix).
