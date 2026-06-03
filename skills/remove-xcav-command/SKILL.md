---
name: "remove-xcav-command"
description: "Step-by-step procedure for safely removing a subcommand from the xcav codebase (e.g. inline, copy, move). Covers source, headers, build files, tests, and documentation."
version: 1
created: "2026-06-03"
---
## When to Use
When removing a subcommand from the xcav codebase (e.g. inline, copy, move).

## Procedure
1. Survey all references: `grep -rn 'CommandName\|command_file\|xcav/command_file' xcav/ --include='*.h' --include='*.cc' --include='*.txt' --include='*.inl'`
2. Use `xcav_blocks` to find the CmdXxx function line number in main.cc
3. Use `xcav_delete` to delete the CmdXxx function from main.cc
4. Use `xcav_edit` to remove `#include "xcav/command_file.h"` from main.cc — use raw text from sed, not xcav_read
5. Use `xcav_edit` to remove the dispatch line from Run(): `else if (ByteviewEq(args.command, "command"))`
6. Use `xcav_edit` to remove help text from CmdHelp (both the detailed section and the overview list entry)
7. Update `xcav/edit_ops.h` if it references the command
8. Update `xcav/editor.h` comment if it references the command
9. Use `xcav_edit` to remove the command_file.cc entry from CMakeListsGenerated.txt
10. Remove the actual source and header files: `rm xcav/command_file.h xcav/command_file.cc`
11. Update `xcav/onboard.inl` to remove the command's documentation section
12. Remove test helper functions and test calls from `xcav/tests/run_tests.sh`
13. Build and test: `cmake --build build/linux-debug --target xcav && XC_BIN=/usr/local/bin/xcav ./xcav/tests/run_tests.sh`
## Pitfalls
- xcav_edit oldText must come from sed/cat/raw file, NOT from xcav_read structural output (which un-indents)
- #include lines are not structural blocks — xcav_delete won't work on them, use xcav_edit instead
- After deleting a function in main.cc, line numbers shift — always run xcav_blocks between sequential xcav_delete calls
- Unicode box-drawing comment separators (// ───) use actual Unicode chars, not ASCII dashes

## Verification
1. grep -rn 'CmdXxx\|command_file' xcav/ returns no results after removal
2. cmake --build build/linux-debug --target xcav succeeds
3. XC_BIN=/usr/local/bin/xcav ./xcav/tests/run_tests.sh shows same pass count as before
